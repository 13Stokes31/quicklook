//! Decodificación acotada y escalado Lanczos con composición sobre fondo opaco.

use std::path::Path;

use anyhow::{ensure, Context, Result};
use fast_image_resize::images::{Image as FirImage, ImageRef};
use fast_image_resize::{PixelType, ResizeAlg, ResizeOptions, Resizer};
use image::{DynamicImage, ImageDecoder, ImageReader, Limits, RgbaImage};

pub const IMAGE_SCREEN_FRACTION: f64 = 0.85;
pub const FRAME_SCREEN_FRACTION: f64 = 0.90;

const MAX_IMAGE_BYTES: u64 = 128 * 1024 * 1024;
const MAX_DECODE_BYTES: u64 = 256 * 1024 * 1024;
const MAX_DIMENSION: u32 = 32_768;
const MAX_SURFACE_PIXELS: u64 = 16 * 1024 * 1024;

#[derive(Debug)]
pub struct LoadedImage {
    rgba: RgbaImage,
    has_alpha: bool,
    pub width: u32,
    pub height: u32,
}

#[derive(Clone, Copy, Debug, Default)]
#[repr(C)]
pub struct ImageRect {
    pub x: u32,
    pub y: u32,
    pub width: u32,
    pub height: u32,
}

pub fn image_rect(dst_w: u32, dst_h: u32, src_w: u32, src_h: u32) -> ImageRect {
    let scale = (f64::from(dst_w) / f64::from(src_w)).min(f64::from(dst_h) / f64::from(src_h))
        * (IMAGE_SCREEN_FRACTION / FRAME_SCREEN_FRACTION);
    let width = (f64::from(src_w) * scale)
        .round()
        .clamp(1.0, f64::from(dst_w)) as u32;
    let height = (f64::from(src_h) * scale)
        .round()
        .clamp(1.0, f64::from(dst_h)) as u32;
    ImageRect {
        x: (dst_w - width) / 2,
        y: (dst_h - height) / 2,
        width,
        height,
    }
}

fn validate_dimensions(width: u32, height: u32) -> Result<()> {
    ensure!(width > 0 && height > 0, "imagen con dimensiones vacías");
    ensure!(
        width <= MAX_DIMENSION && height <= MAX_DIMENSION,
        "la imagen supera el límite de {MAX_DIMENSION} píxeles por lado"
    );
    ensure!(
        u64::from(width) * u64::from(height) * 4 <= MAX_IMAGE_BYTES,
        "la imagen supera el límite de 128 MiB en RGBA"
    );
    Ok(())
}

fn decode_limits() -> Limits {
    let mut limits = Limits::default();
    limits.max_image_width = Some(MAX_DIMENSION);
    limits.max_image_height = Some(MAX_DIMENSION);
    limits.max_alloc = Some(MAX_DECODE_BYTES);
    limits
}

impl LoadedImage {
    pub fn load(path: &Path) -> Result<Self> {
        let mut reader = ImageReader::open(path)
            .with_context(|| format!("no se pudo abrir {}", path.display()))?
            .with_guessed_format()?;
        reader.limits(decode_limits());
        let mut decoder = reader
            .into_decoder()
            .with_context(|| format!("no se pudo leer {}", path.display()))?;
        let (width, height) = decoder.dimensions();
        validate_dimensions(width, height)?;
        // Reservar también la salida: hay formatos con 16/32 bits por canal.
        let mut limits = decode_limits();
        limits.reserve(decoder.total_bytes())?;
        decoder.set_limits(limits)?;
        let alpha_format = decoder.color_type().has_alpha();
        let rgba = DynamicImage::from_decoder(decoder)
            .with_context(|| format!("no se pudo decodificar {}", path.display()))?
            .into_rgba8();
        let has_alpha = alpha_format && rgba.as_raw().chunks_exact(4).any(|px| px[3] != 255);
        Ok(Self {
            rgba,
            has_alpha,
            width,
            height,
        })
    }

    pub fn memory_bytes(&self) -> usize {
        self.rgba.as_raw().len()
    }

    #[allow(dead_code)] // Usado por el ejecutable original y las comprobaciones de composición.
    pub fn to_softbuffer(&self, dst_w: u32, dst_h: u32, background: u32) -> Result<Vec<u32>> {
        self.render_pixels(dst_w, dst_h, background, false)
    }

    /// Qt pinta el marco por separado; solo necesita el rectángulo opaco RGB32.
    pub fn to_qt_buffer(&self, dst_w: u32, dst_h: u32, background: u32) -> Result<Vec<u32>> {
        self.render_pixels(dst_w, dst_h, background, true)
    }

    fn render_pixels(&self, dst_w: u32, dst_h: u32, background: u32, qt: bool) -> Result<Vec<u32>> {
        ensure!(dst_w > 0 && dst_h > 0, "superficie vacía");
        let pixels = u64::from(dst_w) * u64::from(dst_h);
        ensure!(pixels <= MAX_SURFACE_PIXELS, "superficie demasiado grande");
        let ImageRect {
            x: x_offset,
            y: y_offset,
            width,
            height,
        } = image_rect(dst_w, dst_h, self.width, self.height);
        // ImageRef evita duplicar la imagen original en cada escalado.
        let src = ImageRef::new(self.width, self.height, self.rgba.as_raw(), PixelType::U8x4)?;
        let mut dst = FirImage::new(width, height, PixelType::U8x4);
        let options = ResizeOptions::new()
            .resize_alg(ResizeAlg::Convolution(
                fast_image_resize::FilterType::Lanczos3,
            ))
            .use_alpha(self.has_alpha);
        Resizer::new().resize(&src, &mut dst, &options)?;

        let alpha_byte = if qt { 0xff000000 } else { 0 };
        let (stride, offset, count) = if qt {
            (width as usize, 0, width as usize * height as usize)
        } else {
            (
                dst_w as usize,
                y_offset as usize * dst_w as usize + x_offset as usize,
                pixels as usize,
            )
        };
        let mut packed = vec![background | alpha_byte; count];
        for (y, row) in dst.buffer().chunks_exact(width as usize * 4).enumerate() {
            let start = offset + y * stride;
            let output = &mut packed[start..start + width as usize];
            if !self.has_alpha {
                for (out, px) in output.iter_mut().zip(row.chunks_exact(4)) {
                    *out = alpha_byte
                        | (u32::from(px[0]) << 16)
                        | (u32::from(px[1]) << 8)
                        | u32::from(px[2]);
                }
            } else {
                for (out, px) in output.iter_mut().zip(row.chunks_exact(4)) {
                    let a = u32::from(px[3]);
                    let blend = |c: u8, shift: u32| {
                        let channel = (background >> shift) & 0xff;
                        (u32::from(c) * a + channel * (255 - a)) / 255
                    };
                    *out = alpha_byte
                        | (blend(px[0], 16) << 16)
                        | (blend(px[1], 8) << 8)
                        | blend(px[2], 0);
                }
            }
        }
        Ok(packed)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::test_support::TestDir;
    use image::Rgba;

    #[test]
    fn rejects_large_images_before_decoding_pixels() {
        assert!(validate_dimensions(0, 1).is_err());
        assert!(validate_dimensions(MAX_DIMENSION + 1, 1).is_err());
        assert!(validate_dimensions(8192, 8192).is_err());
        let dir = TestDir::new();
        let path = dir.path.join("wide.png");
        RgbaImage::new(MAX_DIMENSION + 1, 1).save(&path).unwrap();
        assert!(LoadedImage::load(&path).is_err());
    }

    #[test]
    fn guesses_format_for_explicit_unknown_extension() {
        let dir = TestDir::new();
        let png = dir.image("source.png", 3, 2);
        let unknown = dir.path.join("preview.unknown");
        std::fs::copy(png, &unknown).unwrap();
        let image = LoadedImage::load(&unknown).unwrap();
        assert_eq!(
            (image.width, image.height, image.memory_bytes()),
            (3, 2, 24)
        );
    }

    #[test]
    fn composites_alpha_and_keeps_aspect_ratio() {
        let background = 0x123456;
        let image = LoadedImage {
            rgba: RgbaImage::from_pixel(2, 1, Rgba([255, 0, 0, 255])),
            has_alpha: false,
            width: 2,
            height: 1,
        };
        assert_eq!(
            image.to_softbuffer(2, 3, background).unwrap(),
            vec![background, background, 0xff0000, 0xff0000, background, background]
        );
        let transparent = LoadedImage {
            rgba: RgbaImage::from_pixel(1, 1, Rgba([255, 0, 0, 0])),
            has_alpha: true,
            width: 1,
            height: 1,
        };
        assert_eq!(
            transparent.to_softbuffer(1, 1, background).unwrap(),
            vec![background]
        );
        let half = LoadedImage {
            rgba: RgbaImage::from_pixel(1, 1, Rgba([255, 0, 0, 128])),
            has_alpha: true,
            width: 1,
            height: 1,
        };
        assert_eq!(
            half.to_softbuffer(1, 1, background).unwrap(),
            vec![0x88192a]
        );
    }

    #[test]
    fn image_keeps_a_visible_frame_even_with_matching_proportions() {
        let image = LoadedImage {
            rgba: RgbaImage::from_pixel(1, 1, Rgba([255, 0, 0, 255])),
            has_alpha: false,
            width: 1,
            height: 1,
        };
        let pixels = image.to_softbuffer(180, 180, 0xffffff).unwrap();
        assert_eq!(
            pixels.iter().filter(|&&color| color == 0xff0000).count(),
            170 * 170
        );
        assert_eq!(pixels[4 * 180 + 90], 0xffffff);
        assert_eq!(pixels[5 * 180 + 90], 0xff0000);
        assert_eq!(pixels[90 * 180 + 4], 0xffffff);
        assert_eq!(pixels[90 * 180 + 5], 0xff0000);
        assert_eq!(pixels[175 * 180 + 90], 0xffffff);
        assert_eq!(pixels[90 * 180 + 175], 0xffffff);
    }

    #[test]
    fn rejects_empty_or_huge_surface_without_allocating() {
        let image = LoadedImage {
            rgba: RgbaImage::new(1, 1),
            has_alpha: true,
            width: 1,
            height: 1,
        };
        assert!(image.to_softbuffer(0, 1, 0xffffff).is_err());
        assert!(image.to_softbuffer(u32::MAX, u32::MAX, 0xffffff).is_err());
    }

    #[test]
    fn opaque_fast_path_keeps_lanczos_pixels_and_qt_excludes_the_frame() {
        let rgba = RgbaImage::from_fn(7, 5, |x, y| {
            Rgba([(x * 31) as u8, (y * 47) as u8, (x * y * 5) as u8, 255])
        });
        let image = LoadedImage {
            rgba: rgba.clone(),
            has_alpha: false,
            width: 7,
            height: 5,
        };
        let legacy = LoadedImage {
            rgba,
            has_alpha: true,
            width: 7,
            height: 5,
        };
        let full = image.to_softbuffer(23, 31, 0x123456).unwrap();
        assert_eq!(full, legacy.to_softbuffer(23, 31, 0x123456).unwrap());
        let qt = image.to_qt_buffer(23, 31, 0x123456).unwrap();
        let rect = image_rect(23, 31, 7, 5);
        assert_eq!(qt.len(), (rect.width * rect.height) as usize);
        for (y, row) in qt.chunks_exact(rect.width as usize).enumerate() {
            let start = (rect.y as usize + y) * 23 + rect.x as usize;
            for (new, old) in row.iter().zip(&full[start..start + rect.width as usize]) {
                assert_eq!(*new, *old | 0xff000000);
            }
        }
    }
}

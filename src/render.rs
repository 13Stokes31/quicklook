//! Carga y escalado de imágenes de alta calidad.
//!
//! Decodifica con `image` y reescala con `fast_image_resize` (Lanczos3) para
//! previsualizar imágenes de gran resolución sin aliasing. El resultado es un
//! buffer RGBA listo para empaquetar al formato 0RGB de softbuffer.

use std::num::NonZeroU32;
use std::path::Path;

use anyhow::{Context, Result};
use fast_image_resize::images::Image as FirImage;
use fast_image_resize::{PixelType, ResizeAlg, ResizeOptions, Resizer};
use image::DynamicImage;

/// Imagen ya decodificada en RGBA8, junto con sus dimensiones nativas.
pub struct LoadedImage {
    rgba: DynamicImage,
    pub width: u32,
    pub height: u32,
}

impl LoadedImage {
    /// Decodifica una imagen del disco. No reescala todavía.
    pub fn load(path: &Path) -> Result<Self> {
        let img = image::open(path)
            .with_context(|| format!("no se pudo decodificar la imagen: {}", path.display()))?;
        let width = img.width();
        let height = img.height();
        Ok(Self {
            rgba: img,
            width,
            height,
        })
    }

    /// Relación de aspecto (ancho / alto). Usado para dimensionar la ventana.
    pub fn aspect_ratio(&self) -> f64 {
        self.width as f64 / self.height as f64
    }

    /// Reescala la imagen a `dst_w` × `dst_h` con Lanczos3 y devuelve el buffer
    /// empaquetado en u32 (formato 0RGB que espera softbuffer: 0x00RRGGBB).
    pub fn to_softbuffer(&self, dst_w: u32, dst_h: u32) -> Result<Vec<u32>> {
        let src = self.rgba.to_rgba8();
        let src_image = FirImage::from_vec_u8(
            NonZeroU32::new(self.width).context("ancho de origen 0")?.get(),
            NonZeroU32::new(self.height).context("alto de origen 0")?.get(),
            src.into_raw(),
            PixelType::U8x4,
        )?;

        let mut dst_image = FirImage::new(
            NonZeroU32::new(dst_w).context("ancho de destino 0")?.get(),
            NonZeroU32::new(dst_h).context("alto de destino 0")?.get(),
            PixelType::U8x4,
        );

        let mut resizer = Resizer::new();
        let opts = ResizeOptions::new().resize_alg(ResizeAlg::Convolution(
            fast_image_resize::FilterType::Lanczos3,
        ));
        resizer.resize(&src_image, &mut dst_image, &opts)?;

        // Empaquetar RGBA8 -> 0x00RRGGBB componiendo sobre un gris oscuro.
        // (softbuffer no admite alpha de ventana; la translucidez real de KWin
        // exigiría migrar a una superficie ARGB con wgpu. Ver plan Fase render.)
        const BG: u32 = 0x22; // gris oscuro ~#222222 para zonas transparentes
        let bytes = dst_image.buffer();
        let mut packed = Vec::with_capacity((dst_w * dst_h) as usize);
        for px in bytes.chunks_exact(4) {
            let (r, g, b, a) = (px[0] as u32, px[1] as u32, px[2] as u32, px[3] as u32);
            // Composición "source-over" sobre el fondo gris: out = src*a + bg*(1-a).
            let inv = 255 - a;
            let r = (r * a + BG * inv) / 255;
            let g = (g * a + BG * inv) / 255;
            let b = (b * a + BG * inv) / 255;
            packed.push((r << 16) | (g << 8) | b);
        }
        Ok(packed)
    }
}

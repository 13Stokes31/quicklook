//! Ventana sin marco, estados de carga/error y resultados asíncronos del visor.

use std::num::NonZeroU32;
use std::path::PathBuf;
use std::rc::Rc;
use std::sync::Arc;

use anyhow::{anyhow, Result};
use winit::application::ApplicationHandler;
use winit::dpi::{LogicalSize, PhysicalPosition};
use winit::event::WindowEvent;
use winit::event_loop::{ActiveEventLoop, EventLoop};
use winit::keyboard::{Key, NamedKey};
use winit::platform::wayland::WindowAttributesExtWayland;
use winit::window::{Window, WindowId};

use crate::nav::Directory;
use crate::prefetch::{ImageEvent, Prefetcher};
use crate::render::{LoadedImage, FRAME_SCREEN_FRACTION};
use crate::theme::window_background;

const APP_ID: &str = "org.quicklook.Viewer";

enum Preview {
    Loading,
    Ready(Arc<LoadedImage>),
    Failed(String),
}

pub struct Viewer {
    preview: Preview,
    request_id: u64,
    dir: Directory,
    prefetch: Prefetcher,
    window: Option<Rc<Window>>,
    surface: Option<softbuffer::Surface<Rc<Window>, Rc<Window>>>,
    context: Option<softbuffer::Context<Rc<Window>>>,
    scaled: Option<(u32, u32, Vec<u32>)>,
    requested_size: Option<LogicalSize<f64>>,
    fatal_error: Option<String>,
    focused_once: bool,
    background: u32,
}

fn fixed_window_size(available: LogicalSize<f64>) -> LogicalSize<f64> {
    LogicalSize::new(
        (available.width * FRAME_SCREEN_FRACTION).max(1.0),
        (available.height * FRAME_SCREEN_FRACTION).max(1.0),
    )
}

fn draw_placeholder(pixels: &mut [u32], width: u32, height: u32, failed: bool, background: u32) {
    pixels.fill(background);
    let cx = i64::from(width / 2);
    let cy = i64::from(height / 2);
    let radius = i64::from(width.min(height).min(48) / 2);
    for y in -radius..=radius {
        for x in -radius..=radius {
            let marked = if failed {
                (x - y).abs() <= 2 || (x + y).abs() <= 2
            } else {
                [-12, 0, 12]
                    .iter()
                    .any(|center| (x - center).pow(2) + y.pow(2) <= 9)
            };
            let (px, py) = (cx + x, cy + y);
            if marked && px >= 0 && px < i64::from(width) && py >= 0 && py < i64::from(height) {
                pixels[py as usize * width as usize + px as usize] =
                    if failed { 0xe57373 } else { 0xaaaaaa };
            }
        }
    }
}

impl Viewer {
    fn new(path: PathBuf, on_loaded: impl Fn(ImageEvent) + Send + 'static) -> Result<Self> {
        let mut viewer = Self {
            preview: Preview::Loading,
            request_id: 0,
            dir: Directory::new(path),
            prefetch: Prefetcher::new(on_loaded)?,
            window: None,
            surface: None,
            context: None,
            scaled: None,
            requested_size: None,
            fatal_error: None,
            focused_once: false,
            background: window_background(),
        };
        viewer.request_current();
        Ok(viewer)
    }

    fn request_current(&mut self) {
        self.request_id = self.request_id.wrapping_add(1);
        self.preview = Preview::Loading;
        self.scaled = None;
        self.prefetch.request(
            self.request_id,
            self.dir.current().to_path_buf(),
            [self.dir.peek_next(), self.dir.peek_prev()]
                .into_iter()
                .flatten(),
        );
        self.update_window();
    }

    fn navigate(&mut self, next: bool) {
        if if next {
            self.dir.next()
        } else {
            self.dir.prev()
        } {
            self.request_current();
        }
    }

    fn focus_changed(&mut self, focused: bool) -> bool {
        if focused {
            self.focused_once = true;
        }
        // Al crear la ventana puede recibirse un primer Focused(false).
        // Solo cerrar cuando se pierda un foco que realmente se obtuvo.
        !focused && self.focused_once
    }

    fn accept_result(&mut self, event: ImageEvent) -> bool {
        if event.id != self.request_id || event.path != self.dir.current() {
            return false;
        }
        self.preview = match event.result {
            Ok(image) => Preview::Ready(image),
            Err(error) => {
                eprintln!("no se pudo cargar {}: {error}", event.path.display());
                Preview::Failed(error)
            }
        };
        self.scaled = None;
        self.update_window();
        true
    }

    pub fn run(path: PathBuf) -> Result<()> {
        let event_loop = EventLoop::<ImageEvent>::with_user_event().build()?;
        let proxy = event_loop.create_proxy();
        let mut viewer = Self::new(path, move |event| {
            let _ = proxy.send_event(event);
        })?;
        event_loop.run_app(&mut viewer)?;
        if let Some(error) = viewer.fatal_error {
            return Err(anyhow!(error));
        }
        Ok(())
    }

    fn update_window(&mut self) {
        let Some(window) = &self.window else {
            return;
        };
        let name = self
            .dir
            .current()
            .file_name()
            .unwrap_or_default()
            .to_string_lossy();
        let title = match &self.preview {
            Preview::Loading => format!("{name} — Cargando — Quick Look"),
            Preview::Ready(_) => format!("{name} — Quick Look"),
            Preview::Failed(error) => format!("{name} — No se pudo abrir: {error} — Quick Look"),
        };
        window.set_title(&title);
        // Wayland no proporciona el área de trabajo; dejamos margen para paneles.
        let monitor = window
            .current_monitor()
            .or_else(|| window.available_monitors().next());
        let available = monitor
            .as_ref()
            .map(|m| m.size().to_logical::<f64>(window.scale_factor()))
            .unwrap_or_else(|| LogicalSize::new(1000.0, 800.0));
        let size = fixed_window_size(available);
        let current_size = window.inner_size().to_logical::<u32>(window.scale_factor());
        // KWin puede entregar una configuración inicial tardía después de que
        // el visor haya solicitado otro tamaño. Reaplicar si no es el real.
        if self.requested_size != Some(size) || current_size != size.cast::<u32>() {
            self.requested_size = Some(size);
            // Wayland fija min/max al crear una ventana no redimensionable.
            // Hay que actualizar también esos límites al ajustar el recuadro.
            window.set_min_inner_size(Some(size));
            window.set_max_inner_size(Some(size));
            let _ = window.request_inner_size(size);
        }
        // X11 admite posición absoluta; en Wayland el centrado corresponde a KWin.
        if window.outer_position().is_ok() {
            if let Some(monitor) = monitor {
                let origin = monitor.position();
                let area = monitor.size();
                let outer = window.outer_size();
                window.set_outer_position(PhysicalPosition::new(
                    origin.x + (i64::from(area.width) - i64::from(outer.width)) as i32 / 2,
                    origin.y + (i64::from(area.height) - i64::from(outer.height)) as i32 / 2,
                ));
            }
        }
        window.request_redraw();
    }

    fn redraw(&mut self) -> Result<()> {
        let Some(window) = &self.window else {
            return Ok(());
        };
        let size = window.inner_size();
        let (Some(width), Some(height)) =
            (NonZeroU32::new(size.width), NonZeroU32::new(size.height))
        else {
            return Ok(());
        };
        if let Preview::Ready(image) = &self.preview {
            if !matches!(&self.scaled, Some((w, h, _)) if *w == size.width && *h == size.height) {
                match image.to_softbuffer(size.width, size.height, self.background) {
                    Ok(pixels) => self.scaled = Some((size.width, size.height, pixels)),
                    Err(error) => {
                        eprintln!("error al escalar: {error:#}");
                        self.preview = Preview::Failed(format!("{error:#}"));
                        self.scaled = None;
                        self.update_window();
                    }
                }
            }
        }
        let Some(surface) = self.surface.as_mut() else {
            return Ok(());
        };
        surface
            .resize(width, height)
            .map_err(|e| anyhow!("no se pudo ajustar la superficie: {e}"))?;
        let mut buffer = surface
            .buffer_mut()
            .map_err(|e| anyhow!("no se pudo obtener el buffer: {e}"))?;
        if let Some((_, _, pixels)) = &self.scaled {
            buffer.copy_from_slice(pixels);
        } else {
            draw_placeholder(
                &mut buffer,
                size.width,
                size.height,
                matches!(self.preview, Preview::Failed(_)),
                self.background,
            );
        }
        buffer
            .present()
            .map_err(|e| anyhow!("no se pudo presentar la imagen: {e}"))?;
        Ok(())
    }
}

impl ApplicationHandler<ImageEvent> for Viewer {
    fn resumed(&mut self, event_loop: &ActiveEventLoop) {
        if self.window.is_some() {
            return;
        }
        let attrs = Window::default_attributes()
            .with_name(APP_ID, "viewer")
            .with_title("Cargando — Quick Look")
            .with_decorations(false)
            .with_resizable(false)
            .with_inner_size(LogicalSize::new(320.0, 240.0));
        let setup = (|| {
            let window = Rc::new(event_loop.create_window(attrs)?);
            let context = softbuffer::Context::new(window.clone())
                .map_err(|e| anyhow!("no se pudo crear el contexto gráfico: {e}"))?;
            let surface = softbuffer::Surface::new(&context, window.clone())
                .map_err(|e| anyhow!("no se pudo crear la superficie: {e}"))?;
            Ok::<_, anyhow::Error>((window, context, surface))
        })();
        match setup {
            Ok((window, context, surface)) => {
                self.window = Some(window);
                self.context = Some(context);
                self.surface = Some(surface);
                self.update_window();
            }
            Err(error) => {
                self.fatal_error = Some(format!("{error:#}"));
                event_loop.exit();
            }
        }
    }

    fn user_event(&mut self, _event_loop: &ActiveEventLoop, event: ImageEvent) {
        self.accept_result(event);
    }

    fn window_event(&mut self, event_loop: &ActiveEventLoop, _id: WindowId, event: WindowEvent) {
        match event {
            WindowEvent::CloseRequested => event_loop.exit(),
            WindowEvent::Focused(focused) => {
                if self.focus_changed(focused) {
                    event_loop.exit();
                }
            }
            WindowEvent::RedrawRequested => {
                if let Err(error) = self.redraw() {
                    eprintln!("error gráfico: {error:#}");
                }
            }
            WindowEvent::Resized(_) | WindowEvent::ScaleFactorChanged { .. } => {
                self.update_window()
            }
            WindowEvent::KeyboardInput {
                event,
                is_synthetic: false,
                ..
            } if event.state.is_pressed() => {
                match event.logical_key {
                    Key::Named(NamedKey::Escape) => event_loop.exit(),
                    // No cerrar por la repetición del Espacio que abrió el visor.
                    Key::Named(NamedKey::Space) if !event.repeat => event_loop.exit(),
                    Key::Named(NamedKey::ArrowRight | NamedKey::ArrowDown) => self.navigate(true),
                    Key::Named(NamedKey::ArrowLeft | NamedKey::ArrowUp) => self.navigate(false),
                    _ => {}
                }
            }
            _ => {}
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::test_support::TestDir;

    #[test]
    fn window_fits_small_and_scaled_monitors() {
        for (monitor, expected) in [
            ((640.0, 360.0), (576.0, 324.0)),
            ((1920.0, 1080.0), (1728.0, 972.0)),
            ((1080.0, 1920.0), (972.0, 1728.0)),
            ((80.0, 60.0), (72.0, 54.0)),
            ((1.0, 1.0), (1.0, 1.0)),
        ] {
            assert_eq!(
                fixed_window_size(LogicalSize::new(monitor.0, monitor.1)),
                LogicalSize::new(expected.0, expected.1)
            );
        }
    }

    #[test]
    fn placeholder_replaces_previous_image_and_handles_tiny_surface() {
        let mut pixels = vec![0xffffff; 64 * 64];
        draw_placeholder(&mut pixels, 64, 64, true, 0x123456);
        assert!(!pixels.contains(&0xffffff));
        assert!(pixels.contains(&0xe57373));
        draw_placeholder(&mut pixels, 64, 64, false, 0x123456);
        assert!(!pixels.contains(&0xe57373));
        assert!(pixels.contains(&0xaaaaaa));
        draw_placeholder(&mut [0], 1, 1, true, 0x123456);
    }

    #[test]
    fn stale_results_cannot_replace_selection_and_errors_clear_image() {
        let dir = TestDir::new();
        let first = dir.image("1.png", 2, 3);
        let second = dir.image("2.png", 3, 2);
        let image = Arc::new(LoadedImage::load(&first).unwrap());
        let mut viewer = Viewer::new(first.clone(), |_| {}).unwrap();
        assert!(viewer.accept_result(ImageEvent {
            qt_frame: None,
            id: 1,
            path: first.clone(),
            result: Ok(image.clone())
        }));
        assert!(matches!(viewer.preview, Preview::Ready(_)));
        viewer.navigate(true);
        assert_eq!(viewer.dir.current(), second);
        assert!(matches!(viewer.preview, Preview::Loading));
        assert!(!viewer.accept_result(ImageEvent {
            qt_frame: None,
            id: 1,
            path: first,
            result: Ok(image.clone())
        }));
        assert!(matches!(viewer.preview, Preview::Loading));
        assert!(viewer.accept_result(ImageEvent {
            qt_frame: None,
            id: 2,
            path: second.clone(),
            result: Err("bad image".into())
        }));
        assert!(matches!(viewer.preview, Preview::Failed(_)));
        viewer.navigate(false);
        let current = viewer.dir.current().to_path_buf();
        assert!(!viewer.accept_result(ImageEvent {
            qt_frame: None,
            id: 3,
            path: second,
            result: Ok(image.clone())
        }));
        assert!(viewer.accept_result(ImageEvent {
            qt_frame: None,
            id: 3,
            path: current,
            result: Ok(image)
        }));
        assert!(matches!(viewer.preview, Preview::Ready(_)));
    }

    #[test]
    fn initial_unfocused_event_does_not_close_the_window() {
        let dir = TestDir::new();
        let path = dir.image("image.png", 2, 2);
        let mut viewer = Viewer::new(path, |_| {}).unwrap();
        assert!(!viewer.focus_changed(false));
        assert!(!viewer.focus_changed(true));
        assert!(viewer.focus_changed(false));
    }
}

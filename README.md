# quicklook

Visor de imágenes tipo **Quick Look de macOS** para **KDE Plasma 6 / Wayland**:
pulsas una tecla sobre una imagen y se abre al instante en una ventana sin marco,
ajustada a la imagen; con las flechas te mueves por la carpeta y con Escape (o al
hacer clic fuera) se cierra. Rápido y ligero, escrito en Rust.

## Qué hace

- Abre la imagen que le pases en una **ventana sin bordes** ajustada a su relación de
  aspecto, reescalada con calidad (`fast_image_resize`).
- **Navegación por la carpeta** con las flechas (←/↑ anterior, →/↓ siguiente), en el
  mismo **orden natural** que Dolphin (`foto2` antes que `foto10`) y con vuelta al
  principio/fin.
- **Prefetch** de las imágenes vecinas para que el cambio sea instantáneo.
- Se cierra con **Escape** o **al perder el foco** (aproximación al "clic fuera" de macOS).
- Formatos: JPG, PNG, WebP, GIF, BMP, TIFF, AVIF (y SVG en la lista de navegación).

## Compilar

```bash
# Uso diario:
cargo build --release          # → target/release/quicklook

# Distribución (AUR): binario mínimo y máximo rendimiento
cargo build --profile dist     # → target/dist/quicklook
```

## Uso

```bash
quicklook <ruta-de-imagen>
```

Pensado para lanzarse desde un **menú de servicio de Dolphin** (maneja bien que lo
inviten con una ruta relativa). Idea de integración: asociarlo a una tecla o a la
barra espaciadora sobre la selección, como en macOS.

## Estado

MVP funcional (visor + navegación + prefetch, render por CPU con `softbuffer`).
La variante experimental con blur de KWin y render por GPU (wgpu) vive en un repo
aparte (`quicklook-wgpu`).

## Licencia

GPL-3.0-or-later.

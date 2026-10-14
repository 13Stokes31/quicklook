# Auditoría técnica de `quicklook`

Fecha: 2026-08-29

## Alcance y comprobaciones

- Revisión completa de arquitectura, navegación, carga, prefetch, renderizado y documentación.
- `cargo test --locked`: correcto, pero ejecuta **0 pruebas**.
- `cargo clippy --all-targets --locked -- -D warnings`: correcto.
- No se abrió la GUI durante la auditoría, al requerir una sesión gráfica Wayland.

## Configuración para asistentes

No existen `CLAUDE.md`, `AGENTS.md`, `.claude/`, `.codex/`, skills ni hooks. La modularidad y los comentarios ya dan contexto suficiente; no conviene añadir configuración específica. `PROPUESTA.md` está claramente presentado como propuesta histórica y el README diferencia bien el estado actual.

## Problemas reales que conviene corregir

### Alta — Se anuncian y navegan SVG que el decodificador no soporta

`nav.rs` incluye `svg`, pero `LoadedImage::load` usa `image::open`, que no renderiza SVG. Al llegar a uno, `show_current` falla y deja visible la imagen anterior, aunque el índice interno ya avanzó; la siguiente flecha puede saltarse archivos y la ventana muestra un estado incoherente. Debe eliminarse SVG de la lista y del README o incorporarse un renderizador SVG real. Ante cualquier fallo de carga, la navegación debe mostrar error/placeholder o avanzar de forma controlada, no conservar silenciosamente otra imagen.

### Alta — Caché sin límite de imágenes decodificadas

Cada archivo visitado queda para siempre en un `HashMap` como `DynamicImage` RGBA, y se precargan vecinos. Una carpeta con muchas fotos grandes puede consumir varios gigabytes y provocar que el sistema mate el visor. Un Quick Look solo necesita una caché LRU pequeña —actual, anterior y siguiente, quizá alguna más— limitada por bytes, no solo por número de entradas.

### Media — Archivos grandes o maliciosos pueden agotar memoria

Se decodifican imágenes completas sin límite previo de dimensiones/píxeles y después se crea otra copia RGBA más buffers de escalado. Un archivo con dimensiones enormes o una bomba de compresión puede bloquear o agotar memoria. Deben comprobarse metadatos/dimensiones y aplicar límites del decodificador antes de reservar; para fotos enormes conviene decodificación reducida cuando el formato lo permita.

### Media — La UI vuelve a decodificar sincrónicamente si falla el prefetch

Si el vecino aún no está listo, `show_current` ejecuta carga y decodificación en el hilo de eventos. Es justo cuando una imagen grande puede congelar la ventana. La navegación debería mostrar temporalmente un estado de carga y recibir el resultado del worker. También conviene deduplicar solicitudes “en curso”, pues ahora varias peticiones de la misma ruta pueden quedar en la cola antes de entrar en caché.

### Media — Tamaño de ventana fijo sin considerar el monitor

La caja máxima es siempre 900×900 píxeles lógicos. Puede exceder el área útil de pantallas pequeñas o escaladas, y relaciones extremas pueden producir una dimensión casi nula. Debe calcularse respecto al monitor actual/área disponible y fijar dimensiones mínimas.

### Media — No hay pruebas

Se ejecutan cero pruebas. `natural_cmp`, filtrado de extensiones, navegación circular y límites de caché son lógica pura y deberían cubrirse fácilmente. Una prueba con SVG habría detectado la contradicción principal. El render necesita además pruebas de tamaños extremos y alpha con imágenes sintéticas pequeñas.

## Mejoras recomendables pero no necesarias

- Diferenciar visualmente GIF animado tratado como fotograma estático o documentar esa limitación.
- Propagar los errores de creación de `softbuffer` en vez de usar `expect`; es un fallo de entorno recuperable, no una invariancia.
- Registrar errores de `surface.resize`, `buffer_mut` y `present` en lugar de ignorarlos completamente.
- Hacer determinista el desempate natural de nombres con ceros a la izquierda (`foto1`, `foto01`).
- Añadir el Service Menu/atajo solo cuando exista una integración instalable y reversible; el README acierta al no prometerla todavía.
- Limpiar `target/` si no se necesita: ocupa alrededor de 1,4 GB y está correctamente ignorado.

## Aspectos que están bien y no merece la pena tocar

- División pequeña y clara entre navegación, carga/render, prefetch y bucle de ventana.
- El archivo inicial falla con un error explícito en vez de abrir una ventana vacía.
- Escalado Lanczos cacheado por tamaño y composición alpha correcta sobre fondo opaco.
- Navegación natural y circular adecuada para el caso de uso.
- El prefetch en un único worker evita crear un hilo por imagen.
- La aplicación no ejecuta contenido de los archivos ni usa red/comandos externos.
- El alcance documentado es realista respecto a Dolphin y Wayland.
- `Cargo.lock` está versionado y Clippy pasa sin avisos.

## Conclusión

La base es simple y adecuada, pero hay dos fallos concretos antes de ampliar formatos: SVG figura como soportado sin serlo y la caché puede crecer hasta agotar memoria. Tras acotar memoria y mantener toda decodificación fuera del hilo gráfico, no se necesita una reescritura ni migrar a `wgpu` para cumplir el visor de imágenes actual.

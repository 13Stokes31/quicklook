# Propuesta de Proyecto: Quick Look Nativo para KDE Plasma 6 (Dolphin)

## Contexto del Proyecto
Desarrollo de una aplicación nativa para **KDE Plasma 6 (entorno Wayland)** en **Arch Linux** que replique la experiencia completa de la función *Quick Look* de macOS dentro del gestor de archivos **Dolphin**. El objetivo final es estructurar el proyecto para poder empaquetarlo y publicarlo como un paquete comunitario en **AUR**.

## El Problema de Navegación y Foco (Crucial en Wayland)
En macOS, al abrir Quick Look, el usuario puede usar las flechas del teclado para seguir navegando por los archivos del directorio, y la previsualización se actualiza automáticamente. En Wayland, al abrir una ventana nueva, esta roba el foco del teclado de forma estricta. 

Para solucionar esto, la aplicación debe:
1. Recibir la ruta del archivo inicialmente seleccionado.
2. Cargar de forma eficiente en memoria el contexto de la carpeta contenedora completa.
3. Capturar las flechas direccionales estando la ventana del visor enfocada, permitiendo al propio visor cambiar de archivo de forma interna, simulando una continuidad total con Dolphin.

## Requisitos Core del Binario
* **Disparador:** Apertura instantánea mediante un *Service Menu* de Dolphin (KIO plugin) al interceptar la barra espaciadora.
* **Cierre Inteligente:** Clausura automática del proceso al volver a pulsar la barra espaciadora, la tecla `Escape`, o al **perder el foco (hacer clic fuera de la ventana)**. En Wayland, esto requiere capturar el evento `FocusLost` de la ventana.
* **Interfaz *Frameless*:** Ventana flotante completamente sin bordes ni decoraciones de KWin (`decorations: false`), cuyas proporciones se adapten exactamente a la relación de aspecto del archivo previsualizado.
* **Calidad Visual:** Escalado de imagen de alta fidelidad (filtrado bilineal o generación de mipmaps) para previsualizar imágenes de gran resolución sin *aliasing* ni dientes de sierra.
* **Escalabilidad Futura:** Arquitectura modular preparada para soportar en versiones posteriores la reproducción de vídeo, visualización de PDFs o renderizado de texto/código con resaltado de sintaxis.

## Stack Tecnológico Propuesto
* **Lenguaje Principal:** **Rust** (para asegurar velocidad de arranque crítica, seguridad de memoria y un binario único ligero).
* **Componentes Gráficos/Ventana:** Uso de `winit` para el manejo nativo de ventanas/eventos en Wayland (captura de `FocusLost` y teclado), combinado con `pixels`, `softbuffer` o el framework `Slint` para el renderizado rápido de la interfaz.

---

## Preguntas para la IA Asistente

1. **Arquitectura y Crates:** ¿Qué stack de Rust recomiendas para este caso de uso? Entre `winit` puro con `softbuffer` o utilizar un framework como `Slint`, ¿cuál ofrece menor tiempo de arranque y mejor manejo de ventanas *frameless* y eventos de foco en Wayland?
2. **Lógica de Navegación Asíncrona:** ¿Cómo estructurarías la lógica de lectura del directorio para que el primer archivo cargue en milisegundos, mientras que la indexación de los archivos adyacentes ocurra en segundo plano sin bloquear la UI?
3. **Integración y Atajos en KDE:** ¿Cómo debería ser el diseño del archivo `.desktop` de KIO y qué estrategia recomiendas (mediante scripts en Bash o manipulación de archivos de configuración de KDE) para remapear de forma limpia el atajo de la barra espaciadora en Dolphin durante la instalación?
4. **Empaquetado AUR:** Proporciona la estructura base del proyecto de Cargo y un ejemplo de archivo `PKGBUILD` inicial que automatice la compilación en modo *release* y la instalación del binario y el Service Menu en las rutas globales del sistema (`/usr/bin/` y `/usr/share/kio/servicemenus/`).

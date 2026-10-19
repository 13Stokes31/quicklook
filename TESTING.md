# Comprobaciones manuales

Comprobaciones de uso real pendientes para la versión 0.4.0. Estas
comprobaciones complementan las pruebas automáticas descritas en README.md.
Las pruebas automáticas del paquete instalado no sustituyen esta revisión manual.

- [ ] En Plasma Wayland, ejecutar `target/qt-build/quicklook-qt README.md`.
  Comprobar que el texto se lee bien, el contenido ocupa el área central del
  85 % y el margen del marco del 90 % conserva el blur suave, nativo u opaco
  disponible. Revisar visualmente las esquinas y que las letras no se desenfoquen.
- [ ] Repetir con un tema claro y uno oscuro: comprobar contraste del texto,
  color de fondo y legibilidad del aviso en un registro de más de 512 KiB.
- [ ] Seleccionar varias líneas con el ratón y copiar con Ctrl+C a un editor.
  Comprobar que el desplazamiento con la rueda resulta cómodo y el texto
  copiado mantiene los acentos y saltos de línea.
- [ ] Probar en un monitor con escala fraccionaria: comprobar tamaño de letra,
  selección con el ratón, barras de desplazamiento y distribución del marco.
- [ ] Con la versión nueva instalada y su menú, seleccionar un
  TXT/Markdown y un PDF en Dolphin y abrirlos con Espacio; comprobar que el menú
  aparece también para documentos y conserva los demás atajos configurados.
- [ ] Repetir la apertura del PDF mediante `~/.local/bin/quicklook` cuando sea
  un enlace a `/usr/bin/quicklook`: debe cargar el módulo PDF instalado.
- [ ] Abrir un PDF real de varias páginas: revisar nitidez del texto, ajuste al
  ancho, comodidad del desplazamiento, indicador de página y blur del margen.
  Probar rueda y AvPág/RePág, incluidas páginas de tamaños distintos.
- [ ] En Dolphin, seleccionar una carpeta y pulsar Espacio. Comprobar nombres,
  tamaños de archivos, tipos y fechas, incluidos ocultos. Verificar que las
  subcarpetas aparecen como entradas, sin expansión con doble clic o teclado,
  y que no se muestran sus contenidos ni se calculan sus tamaños totales.
- [ ] Abrir una carpeta vacía y otra con muchos elementos. Comprobar que el
  listado se carga, permite desplazarse y se cierra con Espacio/Escape.
- [ ] Abrir documentos reales ODT/DOC/DOCX y ODS/XLS/XLSX con tablas, imágenes y
  distintas fuentes. Revisar fidelidad del PDF y paginación de hojas anchas.
- [ ] Con LibreOffice ya abierto, lanzar una vista previa de Office y comprobar
  que no cambia sus ventanas ni el documento original. Cambiar de archivo
  durante la conversión, cerrar y reabrir; comprobar que el visor responde.
- [ ] Con una dependencia opcional ausente, comprobar que Office muestra el
  aviso correspondiente y que imágenes, texto y carpetas siguen disponibles.

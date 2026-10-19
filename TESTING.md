# Comprobaciones manuales

Pendientes sobre la compilación de desarrollo con soporte de texto y PDF. Estas
comprobaciones complementan las pruebas automáticas descritas en README.md.
No están verificadas en la sesión personal ni en el paquete AUR publicado.

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
- [ ] Cuando se autorice instalar la versión nueva y su menú, seleccionar un
  TXT/Markdown y un PDF en Dolphin y abrirlos con Espacio; comprobar que el menú
  aparece también para documentos y conserva los demás atajos configurados.
- [ ] Abrir un PDF real de varias páginas: revisar nitidez del texto, ajuste al
  ancho, comodidad del desplazamiento, indicador de página y blur del margen.
  Probar rueda y AvPág/RePág, incluidas páginas de tamaños distintos.

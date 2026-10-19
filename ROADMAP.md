# Pendientes

## Documentos

- [ ] Validación manual de texto y PDF en Plasma Wayland, temas claro/oscuro y
  escalado fraccionario. Los pasos están en TESTING.md.
- [ ] Validar la ampliación 0.4.0: ODT, DOC, DOCX, ODS, XLS y XLSX mediante
  LibreOffice opcional, autorizado el 19 de octubre de 2026; y carpetas con
  contenido directo, sin expandir ni recorrer subcarpetas. Revisar la paginación
  de hojas anchas y la fidelidad de documentos complejos con los pasos de TESTING.md.
- [ ] Valorar audio/vídeo y miniaturas KIO para otros documentos después de
  verificar el uso de texto y PDF. No incorporar la suite Plasma Fusion como
  dependencia: su visor sirve como referencia, pero nuestra interfaz mantiene
  el marco 90 %, contenido 85 % y blur suave con sus respaldos.
- [x] Publicar la versión 0.3.0 y actualizar `quicklook-bin` en AUR (19 de
  octubre de 2026). Los archivos y huellas de la versión 0.2.0 se conservan.
  Se corrigió y verificó la carga de PDF mediante el enlace del menú personal.
- [ ] Resolver con AUR la reutilización del nombre `quicklook` para el paquete
  de fuentes. La solicitud de adopción PRQ#88354 fue rechazada y la entrada
  antigua eliminada el 19 de octubre de 2026. La distribución disponible utiliza
  `quicklook-bin`; no restaurar la herramienta retirada sin acuerdo con AUR.

## Punto de continuación: trabajo pausado el 19 de octubre de 2026

- Rama `wip/office-folders`: ampliación 0.4.0 en desarrollo. La versión instalada
  sigue siendo `quicklook-bin 0.3.0-1`; no se ha instalado ni publicado 0.4.0.
- Implementados el conversor Office asíncrono con perfil privado, macros
  desactivadas, cancelación de su grupo de procesos, límite de 64 MiB y
  reutilización de la última conversión; y el listado Qt de carpetas de solo
  lectura, incluidos ocultos, sin expansión ni exploración de subcarpetas.
- Comprobado: 48 pruebas Rust del código nuevo (antes de cambiar solo la versión
  del paquete de 0.3.0 a 0.4.0); CTest de documentos con conversor controlado;
  carpetas vacías, contenido directo, enlace circular, cancelación, caché y
  errores. También imagen/texto/PDF en X11 y Espacio sobre carpeta en Dolphin,
  siempre en perfiles privados.
- Wayland privado: texto, PDF, imagen, carpeta y DOCX real, contenido 85 %,
  marco 90 %, blur nativo/opaco y cierre con foco dentro del contenido.
  Informes y capturas en `target/qt-wayland-software-1/`; las capturas de carpeta
  y Office se han inspeccionado. Esto no sustituye las pruebas manuales del perfil.
- Pendiente: completar la compilación Rust 0.4.0 y Clippy, interrumpidos para
  pausar. Volver a enlazar Qt contra el núcleo final: `make build`; configurar
  `QUICKLOOK_BUILD_TESTS=ON` y ejecutar CTest. Comprobar también sin módulo PDF.
- Repetir `officeRealFormats` con
  `QUICKLOOK_OFFICE_FIXTURES=/home/stokes/Proyectos/quicklook/target/office-format-probe/converted`.
  Contiene los seis documentos sintéticos. La primera prueba real de ODT llegó
  a listo en unos 35 segundos, superando su espera de 30; la espera de prueba
  se amplió a 65 segundos y está compilada, pero falta repetir los seis formatos
  integrados y revisar esa demora. La conversión CLI previa de los seis formatos
  tardó 2–3 segundos por archivo; no confundirla con el rendimiento del visor.
- Falta un generador de fixtures reproducible para esta comprobación real. Los
  datos convertidos y los prototipos están en `target/` (ignorado por Git); las
  semillas FODT/FODS están guardadas en `tests/fixtures/office/`.
- Antes de empaquetar: regenerar archivos de publicación 0.4.0 y sus SHA256.
  Las recetas ya indican 0.4.0 y LibreOffice opcional, pero aún conservan las
  huellas de 0.3.0. **No publicarlas ni usarlas para instalar en este estado.**
  Actualizar las huellas y `.SRCINFO`, validar el paquete e instalarlo por el
  procedimiento controlado que omite hooks globales y preserva Dolphin/KWin.
  Ampliar también los MIME del menú personal existente para Office y carpetas.
  Los scripts y respaldos anteriores están en `target/upgrade-0.3.0*`.
- No se ha tocado Ansible ni se ha adaptado el módulo de blur de KWin.

## Pendientes previos

- [ ] Valorar adaptar el módulo de blur suave propio de KWin 6.7.5 a KWin 6.8.
  Esta ampliación no lo modifica. En KWin 6.8 se ha comprobado el respaldo
  nativo → opaco → nativo con documentos en un compositor privado.
- [ ] Verificación específica de HDR y distribución entre monitores con escalas
  distintas. Las medidas y pruebas históricas están descritas en README.md y
  docs/consumo.md; no acreditan estas situaciones.
- [ ] Valorar sincronizar la selección con Dolphin. Actualmente la navegación
  sigue el orden natural de la carpeta, sin seguir sus filtros ni selección.

## Decisiones de esta ampliación

- Texto con Qt Widgets y las dependencias existentes, sin un motor web ni un
  intérprete de Markdown/HTML. Vista previa de UTF-8/Latin-1 limitada a 512 KiB
  y 10 000 líneas. Sin resaltado de sintaxis en esta primera versión.
- PDF con Qt PDF / QPdfView, autorizado como dependencia opcional. Se compila
  como módulo separado y se carga únicamente al abrir PDF. En Arch lo distribuye
  `qt6-webengine`. No incluye entrada de contraseñas, edición ni formularios.
- Las imágenes conservan el trabajador, la precarga y sus límites de caché.
  Cambiar a texto descarta las peticiones pendientes de imágenes; una carga
  ya iniciada puede terminar, pero no sustituye al documento seleccionado.
- La revisión y ampliación se realizan en el proyecto. El paquete instalado y
  la configuración personal de Dolphin/KWin no se actualizan automáticamente.

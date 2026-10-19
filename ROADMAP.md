# Pendientes

## Documentos

- [ ] Validación manual de texto y PDF en Plasma Wayland, temas claro/oscuro y
  escalado fraccionario. Los pasos están en TESTING.md.
- [ ] Añadir vista previa de ODT, DOC, DOCX, ODS, XLS y XLSX. La conversión con
  LibreOffice a PDF se ha probado con archivos sintéticos de los seis formatos
  (aproximadamente 2–3 segundos por conversión en este equipo). Falta autorizar
  LibreOffice como dependencia opcional e integrar conversión asíncrona,
  cancelación y reutilización de resultados. Revisar la paginación de hojas
  anchas y la fidelidad de documentos complejos antes de publicarlo.
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

# Pendientes

## Versión 1.0

- [x] Nombre del elemento arriba a la izquierda del marco, sin cambiar las
  proporciones 85/90 %. Nombres largos acortados y nombre completo al pasar el ratón.
- [x] Navegación Qt por todos los archivos y carpetas del directorio contenedor,
  con aviso de «Formato no compatible» para binarios sin lector. Los errores
  de formatos admitidos y los avisos de dependencias opcionales se conservan.
- [x] Verificar fuentes y binario, instalar 1.0.0-1 y publicar v1.0.0 en GitHub
  (20 de octubre de 2026). Descargas públicas y SHA256 verificados. El ejecutable
  instalado supera las pruebas gráficas y Espacio funciona en un Dolphin privado.
- [x] Publicar 1.0.1 en GitHub e instalar `quicklook-bin 1.0.1-1` localmente
  (20 de octubre de 2026). Commit y etiqueta: `fd9a39f`, `v1.0.1`. Comprobados
  el paquete instalado (33 archivos sin alteraciones), navegación por texto,
  imágenes y archivos no compatibles, PDF, nombres largos y cierre en Xvfb.
  El atajo y la configuración personal de Dolphin/KWin se conservan.
- [x] Cabecera más grande y en negrita, aprobada por el usuario en la captura
  de prueba. Se conservan v1.0.0 y sus archivos sin reemplazarlos.
- [x] Completar la publicación tras el reinicio (20 de octubre de 2026): los
  tres archivos de v1.0.1 se descargaron sin autenticación y coinciden con la
  entrega local; las huellas de fuentes y binario coinciden con SHA256SUMS.
- [x] Publicar `quicklook-bin 1.0.1-1` en AUR, commit `6d6b429`. La página
  oficial anuncia esa versión y enlaza al binario publicado; el clon local está
  sincronizado con `origin/master`. Las primeras consultas RPC aún devolvían
  0.4.0-1, mientras la página ya reflejaba la actualización.
  GitHub conserva la etiqueta `v1.0.1` y los tres archivos de la entrega:
  https://github.com/13Stokes31/quicklook/releases/tag/v1.0.1.
  La verificación pública se conserva en `target/release-public-verification-1.0.1/`.
  El nombre AUR `quicklook` para las fuentes sigue pendiente de resolución.
- [ ] Revisión visual de la cabecera en los temas y escalas de TESTING.md.

## Documentos

- [ ] Validación manual de texto y PDF en Plasma Wayland, temas claro/oscuro y
  escalado fraccionario. Los pasos están en TESTING.md.
- [x] Implementar Office mediante LibreOffice opcional y carpetas con contenido
  directo. Verificados los seis formatos sintéticos, originales sin cambios,
  cancelación, caché, límites y listado sin expansión de subcarpetas.
- [ ] Revisar manualmente documentos complejos y hojas anchas, temas y escalado
  fraccionario con los pasos de TESTING.md.
- [x] Publicar 0.4.0 y actualizar el paquete instalado y el menú personal con los
  MIME de Office y carpetas (20 de octubre de 2026). Fuentes y binario validados;
  descargas públicas y SHA256 comprobados. AUR anunció `quicklook-bin 0.4.0-1`.
  El atajo y la configuración de KWin se conservan; carpeta y DOCX probados con
  el ejecutable instalado mediante el enlace del menú, en un Dolphin privado.
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
- LibreOffice opcional, autorizado el 19 de octubre de 2026, convierte una copia
  temporal del original a PDF con perfil privado, macros desactivadas, límite de
  64 MiB y 60 segundos. La conversión se cancela al navegar o cerrar; la ventana
  reutiliza su última conversión si el archivo no cambia. No modifica el perfil
  de LibreOffice ni el original. Las hojas se muestran paginadas.
- Carpetas con QFileSystemModel y vista de solo lectura: elementos directos,
  incluidos ocultos; las subcarpetas no se expanden ni se exploran y no se
  calculan tamaños recursivos. No necesitan las dependencias opcionales.
- La espera de 35 segundos de la primera prueba Office no se reprodujo el
  20 de octubre: los seis formatos integrados tardaron aproximadamente 1–5
  segundos por archivo sencillo. Esto no garantiza tiempos para documentos
  complejos. `tests/smoke_office.py` permite repetir la prueba con datos propios.
- Las imágenes conservan el trabajador, la precarga y sus límites de caché.
  Cambiar a texto descarta las peticiones pendientes de imágenes; una carga
  ya iniciada puede terminar, pero no sustituye al documento seleccionado.
- La revisión y ampliación se realizan en el proyecto. El paquete instalado y
  la configuración personal de Dolphin/KWin no se actualizan automáticamente.

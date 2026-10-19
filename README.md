# quicklook

Visor de imágenes, texto y PDF tipo Quick Look para KDE Plasma 6 / Wayland, con núcleo Rust.
Abre un archivo en una ventana sin bordes; las flechas recorren los archivos compatibles de
la carpeta y Escape, Espacio o perder el foco cierran la vista previa.

Desde la versión **0.3.0** admite también texto y PDF. PDF es opcional y utiliza
Qt PDF, distribuido en Arch dentro de `qt6-webengine`.

## Qué hace

- Conserva el archivo solicitado, incluso si su extensión no está en la lista de
  navegación. Comprueba el contenido para identificar el formato.
- Navega en orden natural (`foto2` antes que `foto10`), con vuelta al principio/fin.
  El orden no sigue la selección, los filtros ni la ordenación elegida en Dolphin.
- La interfaz Qt admite texto UTF-8 y Latin-1: TXT, Markdown, registros, CSV,
  JSON, XML, configuración y código. Los muestra como **texto literal**, sin
  interpretar HTML, ejecutar código ni descargar recursos. Permite seleccionar
  con el ratón, copiar con Ctrl+C y desplazarse con la rueda o Arriba/Abajo.
  Izquierda/Derecha cambian de archivo incluso con el editor enfocado.
  Lee como máximo **512 KiB** y muestra las primeras **10 000 líneas**, con un
  aviso si recorta el contenido. No admite UTF-16 y rechaza datos binarios.
  La navegación añade extensiones de texto habituales y los nombres README,
  LICENSE, Makefile y Dockerfile; no abre todos los archivos de la carpeta para
  clasificarlos. Un texto solicitado directamente puede tener otra extensión.
  El contenido ocupa un área del 85 % dentro del marco del 90 %, conservando el
  blur del margen. El lector Rust original `quicklook-opaque` sigue admitiendo
  únicamente imágenes.
- El módulo opcional PDF utiliza **Qt PDF / QPdfView**, con páginas en vertical,
  ajuste al ancho y desplazamiento con la rueda o AvPág/RePág. Los PDF dañados o
  protegidos con contraseña muestran un mensaje; no se solicita contraseña en
  esta versión. Izquierda/Derecha recorren archivos y Espacio/Escape cierran.
  El módulo se carga únicamente al abrir un PDF: si falta o no puede cargar sus
  bibliotecas, muestra un aviso y las imágenes y el texto siguen funcionando.
  En Arch, Qt PDF está en `qt6-webengine`; no se usa un navegador ni Qt WebEngine
  para mostrar las páginas.
- Decodifica en un único trabajador, también la imagen inicial. Prioriza la
  selección más reciente y precarga como máximo dos vecinos sin acumular peticiones.
- Muestra tres puntos durante la carga y una cruz roja cuando no puede abrir una
  imagen. El título de la ventana y stderr contienen el archivo y el error.
  Puedes seguir navegando; nunca deja una imagen anterior fingiendo ser la nueva.
- Mantiene un marco del **90 % del ancho y del alto del monitor**, considerando su
  escala, durante la carga y al cambiar de imagen. La imagen se amplía o reduce
  dentro de un área máxima del **85 % del ancho y del alto del monitor**, centrada
  y conservando sus proporciones. Así queda un borde visible incluso cuando la
  imagen tiene las mismas proporciones que el monitor. El espacio sobrante se rellena
  con el color de fondo de las ventanas de KDE, leído al abrir la vista previa
  (`Colors:Window/BackgroundNormal` en `kdeglobals`). Si no está definido o no es
  válido, usa blanco. Los cambios de tema se aplican al volver a abrir el visor.
  Wayland no expone el área útil exacta
  descontando paneles. En X11 la ventana se centra directamente; en KDE Wayland
  el centrado requiere activar el complemento descrito abajo.
- Escala con Lanczos y compone la transparencia sobre el mismo fondo de KDE.
- Admite los formatos raster habilitados por `image`: JPEG, PNG, WebP, GIF, BMP,
  TIFF, ICO, QOI, PNM, TGA, HDR, EXR y farbfeld. GIF se muestra como una
  imagen estática. SVG como imagen, AVIF y documentos de Office no están soportados: la configuración
  actual de `image` permite escribir AVIF, pero no incluye su decodificador nativo.

## Límites de memoria

La caché mantiene como máximo **tres imágenes y 256 MiB** de píxeles RGBA, con
expulsión de la menos utilizada. Una imagen individual puede ocupar como máximo
**128 MiB en RGBA** (unos 33,5 millones de píxeles) y tener hasta 32 768 píxeles por
lado. Estas dimensiones se comprueban antes de decodificar los píxeles.
Se aplican además los límites del decodificador de `image`.

Estos límites no representan un máximo de memoria del proceso: también existen
la imagen visible si salió de la caché, el trabajo del decodificador, los buffers
de escalado y la superficie gráfica. El límite de asignaciones de algunos
decodificadores es aproximado. Las imágenes que excedan los límites muestran un
error; no se reducen automáticamente durante la decodificación.
La interfaz Qt añade una caché de hasta **tres imágenes escaladas y 64 MiB**.
La imagen visible puede seguir viva después de salir de esa caché. El consumo
total del proceso incluye además las bibliotecas de Qt y los búferes de ventana.

## Instalación en Arch Linux (AUR)

- **[`quicklook-bin`](https://aur.archlinux.org/packages/quicklook-bin)** está
  publicado e instala el binario oficial para x86_64 sin compilar Rust.
- El paquete de fuentes **`quicklook`** está preparado, pendiente de
  resolver con AUR la reutilización del nombre de una herramienta retirada.
  Ese nombre todavía no sirve para instalar este visor. Puedes
  compilar el código de esta versión siguiendo **Compilar y usar**.

Ambos incluyen la interfaz Qt actual, el visor original `quicklook-opaque`, el
menú contextual, el script de centrado y un configurador opcional del atajo.
Los paquetes son alternativas. El binario requiere Qt 6.12 o posterior.

```bash
paru -S quicklook-bin
```

**La instalación no cambia tus atajos ni activa complementos en tu perfil.**
Para centrar la ventana en Wayland, activa **Quick Look: centrar vista previa** en **Preferencias
del sistema → Gestión de ventanas → Scripts de KWin**. Para el blur suave, activa
**Quick Look: blur suave y esquinas redondeadas** en **Efectos del escritorio**.
El módulo suave de esta versión admite **KWin 6.7.5**. El paquete desde fuentes
lo compila solo con esa versión; con otras construye el visor sin el módulo.
El paquete binario incluye el módulo 6.7.5; KWin comprueba la versión de su
interfaz antes de cargarlo. No fija ni bloquea la versión de KWin. Al actualizar
KWin se conserva el respaldo **blur nativo → opaco**.

### Aviso sobre el atajo Espacio en Dolphin

En Dolphin 26.04 o posterior, puedes configurar voluntariamente el atajo:

```bash
quicklook-dolphin-shortcut --dry-run
quicklook-dolphin-shortcut
```

**Esto asigna Espacio a Quick Look y retira Espacio del modo de selección de
Dolphin.** El modo de selección sigue disponible desde el menú. Se conservan los
otros atajos y se rechazan conflictos con otras acciones. El script guarda un
respaldo en `~/.local/state/quicklook/atajo-antes-*/` (respeta `XDG_STATE_HOME`).
Abre una nueva ventana de Dolphin para cargar el ajuste.
Para deshacerlo, en **Configurar atajos de teclado** retira Espacio de
**Vista previa con Quick Look** y vuelve a asignarlo al **modo de selección**.
Desinstalar el paquete no modifica el perfil ni restaura automáticamente atajos.

### Si ya tenías la instalación de desarrollo

Un menú en `~/.local/share/kio/servicemenus/quicklook.desktop` tiene prioridad
sobre el del sistema y puede seguir apuntando a `~/.local/bin/quicklook`.
Los ejecutables de `~/.local/bin` y el script de centrado local también pueden
ocultar la instalación del paquete. Guarda o mueve las copias locales antes de
migrar; comprueba `command -v quicklook` y el `Exec` de tu menú local. El paquete
no borra esos archivos ni modifica su configuración. El identificador de la
acción sigue siendo el mismo, de modo que el atajo existente se puede conservar.

## Compilar y usar

La interfaz Qt usa Rust, Qt6 Widgets/DBus y KF6WindowSystem; se necesitan Rust,
CMake, pkg-config, Python 3 y un compilador C++20. Para el módulo suave hacen falta
además las cabeceras de KWin 6.7.5, Qt6Quick, KCoreAddons, KConfig, epoxy y Wayland.
Para compilar el módulo PDF se necesitan además Qt6Pdf y Qt6PdfWidgets
(`qt6-webengine` en Arch). CMake los detecta automáticamente y continúa sin PDF
si faltan. También puedes desactivarlo con `-DQUICKLOOK_PDF=OFF` al configurar
CMake. Instalar Qt PDF después de una compilación que lo omitió requiere
recompilar el módulo; los binarios distribuidos deben incluirlo por separado.

```bash
make build
./target/qt-build/quicklook-qt /ruta/imagen.png
# El efecto suave se compila por separado:
make build-softblur
```

La interfaz Rust original sigue disponible para comparación:

```bash
cargo build --release --locked
./target/release/quicklook /ruta/imagen.png
# El perfil opcional de distribución conserva LTO:
cargo build --profile dist --locked
```

El programa admite una única ruta inicial, incluidas rutas relativas y nombres
con espacios o caracteres no UTF-8. `quicklook --help` muestra los controles.
Para una ruta que coincida con una opción, usa `quicklook -- ./--help`.

## Integración con Dolphin

`integration/quicklook.desktop` añade **Vista previa con Quick Look** al menú de
una única imagen, PDF o archivo de texto local seleccionado. Pasa la ruta como argumento, sin usar un
shell. No cambia las asociaciones de archivos ni la configuración de Dolphin.

Instalación para el usuario, cuando quieras activarlo:

```bash
make install PREFIX="$HOME/.local"
```

Instala la interfaz Qt en `~/.local/bin/quicklook`, el visor original en
`~/.local/bin/quicklook-opaque`, el configurador del atajo y el script de centrado.
Si se ha compilado, instala el módulo PDF en `~/.local/lib/quicklook/quicklook-pdf.so`.
No activa estos ajustes. Instala el menú de servicio en
`~/.local/share/kio/servicemenus/quicklook.desktop`. El menú se instala con permiso
de ejecución, requerido por KDE para servicios del usuario. La instalación usa
Python 3 (solo biblioteca estándar) para generar `Exec` y `TryExec` con la ruta
absoluta del binario, incluidas rutas con espacios. El menú no depende del PATH
de Plasma ni requiere volver a iniciar sesión. Si el menú no aparece, comprueba
su activación en **Configurar Dolphin →
Menú contextual**.

Desde **Dolphin 26.04** puedes asignar un atajo al menú de servicio: abre
**Configurar atajos de teclado**, busca **Vista previa con Quick Look** y asigna
**Espacio**, resolviendo cualquier conflicto que indique Dolphin. El atajo actúa
sobre el archivo seleccionado. Dentro del visor, Espacio vuelve a cerrarlo.
En versiones anteriores funciona el menú contextual, sin prometer un atajo
nativo para esa acción.

Para configurar **Espacio** automáticamente en el perfil de este usuario, después
de instalar el menú y autorizar el cambio:

```bash
make install-shortcut
```

Escribe la acción `servicemenu_quicklook.desktop::quicklookPreview` en
`~/.local/share/kxmlgui5/dolphin/dolphinui.rc` (respeta `XDG_DATA_HOME`). Retira
Espacio del modo de selección de Dolphin para evitar el conflicto; ese modo sigue
disponible desde el menú. Conserva cualquier otro atajo personalizado de esa
acción y rechaza conflictos con otras acciones. Antes de modificar guarda el XML
original, o indica que no existía, en `target/atajo-antes-*/`. Es idempotente y no
configura un atajo global. Abre una nueva ventana de Dolphin para cargar el ajuste.
`python3 integration/configure-shortcut.py --dry-run` muestra los cambios sin escribir.

Verificación con Dolphin real en una pantalla y un perfil aislados:

```bash
python3 tests/smoke_shortcut.py
python3 tests/smoke_shortcut.py --kind text
python3 tests/smoke_shortcut.py --kind pdf
```

La prueba selecciona un archivo, pulsa Espacio para abrirlo en el visor Qt,
lo cierra con Espacio y comprueba que la interfaz de Dolphin conserva sus menús.

Para retirar los ejecutables y el menú (el atajo personalizado se retira desde Dolphin):

```bash
make uninstall PREFIX="$HOME/.local"
```

Para empaquetado, `make install PREFIX=/usr DESTDIR=/ruta/de/empaquetado` permite
preparar la instalación sin escribir en `/usr`. La instalación reemplaza los
archivos de Quick Look que ya existan en el prefijo elegido.
`make install-files` prepara los archivos ya compilados, sin recompilar;
`make install-softblur PREFIX=/usr DESTDIR=/ruta/de/empaquetado` añade el módulo
por separado y no activa el efecto.

### Centrado en KDE Wayland

Wayland deja la posición de las ventanas al compositor. El complemento local
`integration/kwin` centra **únicamente** las ventanas con el identificador de
Quick Look, tanto al abrir como al cambiar las dimensiones de la imagen o el
monitor. No cambia la colocación de otras aplicaciones y permite mover la vista
previa manualmente mientras su tamaño no cambie.

Para activarlo en tu perfil, después de autorizar esos cambios:

```bash
make install-centering PREFIX="$HOME/.local"
kwriteconfig6 --file kwinrc --group Plugins --key quicklook-centerEnabled --type bool true
qdbus6 org.kde.KWin /KWin reconfigure
```

Se instalan dos archivos bajo `~/.local/share/kwin/scripts/quicklook-center` y
se habilita exclusivamente su clave en `~/.config/kwinrc`. No reinicia KWin.
Para retirarlo:

```bash
kwriteconfig6 --file kwinrc --group Plugins --key quicklook-centerEnabled --delete
qdbus6 org.kde.KWin /KWin reconfigure
make uninstall-centering PREFIX="$HOME/.local"
```

Referencias: [menús de servicio de KDE](https://develop.kde.org/docs/apps/dolphin/service-menus/)
y [atajos para acciones desde Dolphin 26.04](https://blogs.kde.org/2026/05/03/dolphin-26.04-release/).

### Interfaz Qt con blur suave independiente

La interfaz principal en `qt/` reutiliza el código Rust de navegación, decodificación,
precarga, límites, caché y escalado Lanczos. No ejecuta un proceso por imagen ni
duplica estas funciones en Qt. Usa el mismo marco del 90 % e imagen encajada en el
85 %, con el color de las ventanas de KDE al 40 % de opacidad. La imagen, incluidas
sus transparencias, sigue compuesta sobre un fondo opaco. Conserva los indicadores
de carga/error, las flechas, Espacio, Escape y el cierre al perder el foco.
El marco tiene esquinas redondeadas de hasta 12 píxeles lógicos, con las esquinas
transparentes también en el blur propio y el nativo. El área de la imagen sigue
encajada en el 85 % y el marco ocupa el 90 %.

El mismo trabajador decodifica y escala la selección y sus dos vecinos; los
resultados antiguos nunca sustituyen la selección nueva. Si una imagen vecina
ya está preparada al tamaño de la pantalla, las flechas la muestran sin estado de
carga. Cambiar la escala o el tamaño del monitor invalida solo la caché escalada.
Qt recibe únicamente los píxeles de la imagen, sin un segundo búfer del marco.
Las imágenes opacas omiten los cálculos de alpha; las transparentes conservan
su composición y el mismo filtro Lanczos. La primera decodificación empieza antes
de inicializar Qt, sin adelantar vecinos hasta conocer la pantalla.

Un efecto específico de **KWin 6.7.5** aplica únicamente a `org.quicklook.SoftBlur`
los parámetros del nivel 3, sin modificar el blur general. La geometría se comunica
por D-Bus como proporciones del cliente; no aparece información técnica en el
título y se admiten diferencias de escala entre Qt y el compositor. Si el efecto
no está disponible, no está listo o usa otro protocolo, usa automáticamente el
**blur nativo de KDE**, con la intensidad general configurada en el escritorio.
Si tampoco está disponible el nativo, el marco pasa al **fondo opaco** de KDE.
La prioridad es **suave 3 propio → nativo → opaco**; nunca aplica ambos efectos a
la vez y vuelve al propio si este se recupera. Las transiciones se comprueban
cada segundo y ambos efectos excluyen la imagen. `--solid` fuerza el fondo opaco.
Una actualización de KWin puede impedir cargar el módulo propio; para recuperar
el suave 3 independiente habrá que revisar y recompilar el efecto, cuya API
nativa no garantiza compatibilidad binaria. El visor y sus respaldos funcionan
sin recompilar ese módulo.

```bash
make build-blur
./target/qt-build/quicklook-qt /ruta/imagen.png
./target/qt-build/quicklook-qt --solid /ruta/imagen.png
```

Requiere Qt6Widgets, Qt6DBus, Qt6Quick, KF6WindowSystem, cabeceras/bibliotecas de KWin, epoxy y
Wayland ya instalados en este equipo. Qt dibuja con CPU; KWin realiza el blur con
su propio contexto de GPU. El código del efecto y sus shaders están en
`integration/kwin-softblur/`, con procedencia y licencia de KDE conservadas.

Antes de activarlo en la sesión personal:

```bash
python3 integration/activate-blur.py
```

Ese comando **solo muestra el plan**. La instalación real necesita autorización:
KWin busca los módulos nativos en las rutas de plugins de Qt, por lo que hay que
copiar un único archivo al directorio del sistema, sin reemplazar archivos de KDE:

```bash
pkexec /usr/bin/install -Dm644 target/softblur-build/plugins/kwin/effects/plugins/quicklook_softblur_v2.so /usr/lib/qt6/plugins/kwin/effects/plugins/quicklook_softblur_v2.so
python3 integration/activate-blur.py --activate
```

El activador verifica la versión de KWin, la huella del módulo y su disponibilidad
antes de sustituir el visor. Guarda `~/.local/bin/quicklook-opaque`, respalda los
archivos de usuario que reemplaza en `target/blur-antes-*/`, instala la interfaz
Qt como `~/.local/bin/quicklook` y actualiza únicamente el script de centrado y
`Plugins/quicklook_softblur_v2Enabled=true`. Desactiva únicamente el efecto
anterior `quicklook_softblur`, conserva su módulo para poder revertir y usa otro
identificador para cargar el actualizado sin reutilizar su biblioteca antigua.
El menú y el atajo Espacio no cambian.
No reinicia KWin ni modifica la intensidad del blur general. Al actualizar una
interfaz Qt instalada previamente, comprueba su huella contra los respaldos y
conserva la copia opaca original.

Para revertir, usando el respaldo indicado al activar:

```bash
python3 integration/activate-blur.py --restore target/blur-antes-XXXXXXXX
```

Restaura los archivos anteriores y solo las claves de sus efectos propios, sin
sobrescribir otras preferencias de KDE. Rechaza archivos que hayan cambiado
después de la activación. El módulo del sistema queda instalado pero inactivo si
no estaba cargado antes; su retirada requiere otra acción administrativa.
El respaldo local de desarrollo 85/90 se conserva en
`target/quicklook-estable-85-90.tar.gz`; no forma parte de la distribución.

Verificación aislada, sin activar el efecto en la sesión personal:

```bash
python3 tests/smoke_x11.py --binary target/qt-build/quicklook-qt
python3 tests/smoke_qt_wayland.py
python3 tests/smoke_qt_wayland.py --scale 1.5
python3 tests/smoke_qt_wayland.py --gpu
python3 tests/smoke_qt_wayland.py --gpu --exercise-activation
python3 tests/smoke_qt_wayland.py --gpu --exercise-update --exercise-activation
```

Las pruebas usan el núcleo y la ventana reales. Comprueban capturas del blur
propio frente al nativo 3 y 15 en otra aplicación, cambios de imagen, recuperación
tras errores, composición opaca de alpha, 120 cambios rápidos, autorepetición de
Espacio, respaldo nativo al descargar el efecto propio, fondo opaco al descargar
también el nativo, recuperación de ambos efectos y cierre por teclado
o pérdida de foco. Las capturas y medidas se guardan en `target/qt-wayland-*/`.
Comprueban también que las esquinas dejan ver el fondo y que la navegación
precargada está lista sin mostrar un indicador de carga. La escala del compositor
anidado con GPU puede venir del anfitrión; se guarda su valor real en los resultados.
La comparación del margen arroja una diferencia media de 0 respecto al nivel 3,
tanto a escala 1 como 1,5. HDR y la distribución entre monitores de distinta escala
siguen pendientes de una prueba específica.
`--exercise-update` requiere el módulo anterior guardado localmente en
`target/medicion-carga/softblur-v1.so`; esa comprobación histórica no es necesaria
para compilar ni usar el paquete.
`--exercise-activation` instala y revierte los archivos solo dentro de un prefijo
temporal de la sesión privada, conservando una preferencia ajena añadida entre
ambas operaciones. Sus respaldos se vinculan al bus privado para evitar usarlos
por error en la sesión personal.

Medición local de consumo (resultados resumidos en [docs/consumo.md](docs/consumo.md)): PNG RGB de 1 y 12 MP,
una imagen por carpeta, salida 1280×800, KWin virtual sobre Intel Arc, fondo
estático y tres repeticiones. Compara el Rust opaco original, Qt anterior opaco,
Qt optimizado opaco y Qt optimizado con blur suave. Los contadores de GPU son
del cliente DRM de ese KWin privado; no incluyen otros programas. Si el kernel
protege estos contadores, el lector requiere autenticación administrativa y solo
consulta estadísticas del proceso privado. PSS reparte las páginas compartidas;
RSS cuenta también las bibliotecas compartidas completas. El tiempo del informe
incluye su temporizador de 100 ms y no mide directamente la primera presentación.
Una imagen por carpeta no incluye el coste de precargar vecinos. Estas medidas
no cuantifican el coste del blur sobre fondos animados.

## Verificación

```bash
cargo fmt --all -- --check
cargo test --locked
cargo clippy --all-targets --locked -- -D warnings
desktop-file-validate integration/quicklook.desktop
cmake -S qt -B target/qt-build -DQUICKLOOK_BUILD_TESTS=ON
cmake --build target/qt-build -j2
ctest --test-dir target/qt-build --output-on-failure
python3 tests/smoke_x11.py --binary target/qt-build/quicklook-qt --documents --pdf
python3 tests/smoke_qt_wayland.py --documents-only
```

Las pruebas cubren el archivo inicial, filtrado y orden de navegación, expulsión
de caché, cola acotada, recuperación tras un archivo inválido, resultados tardíos,
límites de imagen, transparencia, proporciones extremas y tamaño de ventana.
Los archivos de prueba se crean dentro de `target/`.

Si tienes Xvfb, xdotool y libX11, la prueba gráfica aislada comprueba también los
píxeles de la imagen y del error, la navegación y el cierre:

```bash
python3 tests/smoke_x11.py
```

Con las cabeceras de Qt6/KF6, c++ y pkg-config ya instalados, la prueba nativa de
la integración reproduce el error de una sesión sin `~/.local/bin` en el PATH y
comprueba el lanzamiento de la acción corregida, incluidas rutas con espacios:

```bash
python3 tests/smoke_dolphin.py
```

Usa un receptor de argumentos dentro de `target/`, sin abrir el visor ni acceder
a la configuración personal de Plasma.

Para probar tamaño y centrado con un KWin Wayland virtual, se necesitan además
KWin, qdbus6, dbus-run-session y las cabeceras de Qt6DBus:

```bash
python3 tests/smoke_wayland.py
```

Esta prueba crea un bus sin activación de servicios y un perfil privado dentro
de `target/`. Verifica imágenes grandes, pequeñas y verticales con la geometría
real del compositor, sin usar tu sesión de Plasma.

`--documents-only` comprueba texto, PDF e imágenes en un KWin privado con
blur nativo y fondo opaco, sin necesitar el módulo suave compilado para KWin
6.7.5. Comprueba también los atajos con el foco dentro del contenido y guarda
capturas en `target/qt-wayland-software-1/`. Las comprobaciones visuales en la
sesión personal se registran en TESTING.md.

## Alcance pendiente

Los problemas, propuestas y decisiones pendientes se registran en
[ROADMAP.md](ROADMAP.md). Las comprobaciones que requieren una persona están en
[TESTING.md](TESTING.md). El visor recorre la carpeta por su cuenta.
`PROPUESTA.md` conserva la propuesta histórica del proyecto; este README describe
el comportamiento actual.

## Licencia

GPL-3.0-or-later para el visor (véase [LICENSE](LICENSE)).
El efecto derivado de KWin conserva GPL-2.0-or-later, sus atribuciones y la
licencia en [LICENSES/GPL-2.0-or-later.txt](LICENSES/GPL-2.0-or-later.txt).

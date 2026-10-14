# Medición local de consumo, 14 de octubre de 2026

Medianas de tres aperturas de PNG RGB de 4000×3000, con una imagen por carpeta,
salida 1280×800, KWin virtual sobre Intel Arc y fondo estático.

| Variante | RAM proporcional (PSS) | RAM residente (RSS) | Pico RSS | CPU al abrir | GPU de KWin al abrir |
| --- | ---: | ---: | ---: | ---: | ---: |
| Rust original opaco | 59,62 MiB | 64,55 MiB | 116,62 MiB | 0,12 s | 4,977 ms |
| Qt optimizado opaco | 68,23 MiB | 113,74 MiB | 120,61 MiB | 0,19 s | 5,106 ms |
| Qt optimizado suave 3 | 68,39 MiB | 114,17 MiB | 121,04 MiB | 0,20 s | 5,396 ms |

En reposo, el fondo estático dejó los contadores de GPU sin incremento medible.
La CPU mediana fue 0 %, con muestras de hasta 0,33 % para Qt con blur.
PSS reparte las páginas compartidas; RSS incluye las bibliotecas compartidas
completas. La pequeña diferencia de PSS entre opaco y blur puede estar dentro de
la variabilidad entre ejecuciones. Los contadores de GPU corresponden únicamente
al cliente DRM del KWin privado; no representan la carga de toda la máquina.

El escalado de 12 MP pasó de una mediana local de 224 ms a 77 ms al evitar trabajo
de alpha en imágenes opacas y entregar a Qt únicamente el área de la imagen.
Estas medidas no representan la primera presentación en pantalla ni una garantía
de latencia. No incluyen fondos animados, navegación entre muchas imágenes,
HDR ni monitores con distintas escalas. La caché de vecinos puede añadir memoria.
Los resultados completos y el lector usado se conservan localmente bajo
`target/medicion-carga/`; los artefactos de desarrollo no forman parte del paquete.

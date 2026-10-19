# Comprobaciones de la versión 0.4

Fecha: 19 de octubre de 2026. Entorno: Linux, GCC 13.3.0, Python 3.

## Reproducción

```sh
make clean
make
make test
```

La compilación usa `-std=c89 -pedantic -Wall -Wextra -Werror -O2`.
Resultado: cinco ejecutables de pruebas C y 26 casos de interfaz Python pasan.
El registro de esta ejecución está en [validation.txt](validation.txt).

- Núcleo: 8.000 ediciones aleatorias frente a un modelo independiente, búsqueda,
  tabuladores, CRLF, archivos binarios y cruce de 64 KiB.
- Almacenamiento: documento de 1 MiB con reemplazo, inserción, borrado, guardado
  y recarga comparando bytes.
- Fallos: escritura, cierre, renombrado, rollback, permisos y asignación.
- Argumentos: opciones por archivo, rutas absolutas, `--`, límite de ancho.
- Archivos: filtros, directorios, fallo de actualización, rutas Linux donde
  dos puntos y barra inversa son caracteres de nombre, no separadores DOS.
- Interfaz real bajo pseudo-terminal: edición, guardar, copiar/cortar/pegar,
  buscar/reemplazar, Unicode español, solo lectura, ratón, varias vistas y
  redimensionado. Se ejercitan F1, ayuda desde el menú y selector, y Escape para
  salir, guardar, cancelar o descartar. Escape dentro de la ayuda vuelve al
  contexto anterior.

La nueva prueba de ayuda inicialmente esperaba solo el título antes de comprobar
el cuerpo. Se corrigió para esperar también la última línea de ayuda, evitando
una carrera entre el proceso del editor y la lectura de su salida.

## Memoria e inspección visual

El test del núcleo se compiló además con AddressSanitizer y
UndefinedBehaviorSanitizer (`-g -O1 -fsanitize=address,undefined
-fno-omit-frame-pointer`). Pasó con `ASAN_OPTIONS=detect_leaks=0`.
LeakSanitizer no puede funcionar bajo ptrace en este entorno; no se afirma haber
verificado fugas. El registro está en [sanitizers.txt](sanitizers.txt).

`help.png` representa las celdas ANSI capturadas del programa real a 75×24.
Se inspeccionó el resultado: título, 16 líneas y botón caben sin recortarse.
Las pruebas también verifican el recentrado y el cierre con ratón a 100×30.
El renderizador se utilizó para la comprobación y no es una dependencia del editor.

## Alcance

No se ha probado cada terminal Linux, secuencia de teclado, sistema de archivos
ni interacción Unicode. Las pruebas del editor no verifican equivalencia con el
programa histórico. El material binario original y sus pruebas de desempaquetado
no se distribuyen ni se ejecutan con `make test` en esta versión.

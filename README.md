# MS-EDIT Linux

Editor de texto para terminal Linux inspirado en **MS-DOS Editor 2.0.026**.
Versión **0.4**, preparada el 19 de octubre de 2026 a partir de la entrega
0.3.1-dev. Conserva sus menús, selección con ratón, varios documentos y ventanas
divididas. La implementación es nueva, escrita en C; no es el código fuente
original de Microsoft ni una decompilación completa o un clon idéntico.

## Compilar y ejecutar

Necesitas Linux, un compilador C (GCC o Clang), GNU Make y un terminal compatible
con secuencias ANSI/xterm. No se necesitan ncurses, bibliotecas externas,
compiladores DOS, emuladores ni archivos de ayuda. Tamaño mínimo: **75×24**.

```sh
make
./editr DEMOES.TXT
```

`make` genera `editr` en la raíz del proyecto. Python 3 solo es necesario para
las pruebas de interfaz, no para compilar ni ejecutar el editor.

```sh
make test
make clean
make CC=clang
```

El Makefile usa C89 estricto y avisos como errores por defecto. Puedes sustituir
las opciones con `make CFLAGS='-std=c89 -Wall -Wextra -O2'`.

## Uso y atajos

Pulsa **F1** para abrir la ayuda integrada; también está en Help → Commands y
en el selector de archivos. **Escape**, desde la edición, sale igual que Alt+X:
si hay cambios, pregunta si quieres guardar, descartar o cancelar. Dentro de un
menú o diálogo, Escape cierra o cancela ese diálogo y vuelve al editor.

| Acción | Tecla o menú |
|---|---|
| Ayuda | F1 |
| Menús | F10; Alt+F/E/S/V/O/H |
| Guardar | F2; File → Save |
| Nuevo, abrir, guardar como, cerrar | Menú File |
| Salir | Escape; Alt+X |
| Seleccionar | Mayús + movimiento; arrastrar con el ratón |
| Copiar, cortar, pegar | Ctrl+C, Ctrl+X, Ctrl+V |
| Buscar | Ctrl+Q y después F; Search → Find |
| Repetir búsqueda | F3 |
| Reemplazar | Ctrl+Q y después A; Search → Replace |
| Documento anterior/siguiente por número | Alt+1…9 |
| Siguiente documento o vista | F6 |
| Dividir / ajustar / cerrar división | Ctrl+F6 / Ctrl+F8 / Ctrl+F4 |
| Mover por palabras / inicio y final del documento | Ctrl+Izquierda/Derecha / Ctrl+Inicio/Fin |
| Inserción o sobrescritura | Insert |
| Navegar en diálogos | Tab, Mayús+Tab, flechas; Enter confirma |

El portapapeles es interno. El pegado desde el terminal admite *bracketed paste*;
Ctrl+V pega el contenido copiado dentro del editor. Algunos entornos reservan
atajos de teclado antes de que lleguen al programa.

```sh
./editr --help
./editr -R archivo.txt            # Solo lectura
./editr -R uno.txt -W dos.txt     # Opciones para los archivos siguientes
./editr -70 datos.bin            # Vista binaria, 70 bytes por fila
./editr -16 datos.bin -0 notas.txt
./editr -B notas.txt             # Monocromo
./editr -- -nombre-con-guion.txt
./editr '*.txt'                  # Comodín interpretado por el editor
```

Las opciones actuales empiezan por `-`: `-B`, `-R`, `-W`, `-<ancho>`, `-0`,
`-h` y `--help`. `--` termina las opciones. Las rutas que empiezan por `/` siempre
son rutas Linux. Se han retirado `/H` (filas DOS) y `/S` (nombres cortos), que no
tenían efecto útil en Linux. El ancho binario se limita a 1021; los números
muy grandes ya no se truncan a 16 bits. Los filtros mantienen la comparación
ASCII sin distinguir mayúsculas y el comportamiento histórico de `*.*`, que
incluye nombres sin extensión.

## Comportamiento y límites que conviene conocer

- **El guardado en modo texto normaliza a CRLF y añade un salto final si el
  documento no está vacío y carece de él.** Se conserva el comportamiento de
  0.3.1; no se ha cambiado a LF en esta limpieza. Usa modo binario cuando
  necesites conservar todos los bytes y los finales de línea.
- Hasta nueve documentos y 16 MiB por documento. Las rutas están limitadas a
  259 bytes. No hay deshacer/rehacer.
- UTF-8 y caracteres españoles, con conversión CP437 para archivos que no
  validan como UTF-8. No se garantiza un tratamiento completo de grafemas,
  caracteres combinados, emoji o todas las anchuras de terminal. La búsqueda
  sin distinguir mayúsculas no implementa todos los casos Unicode.
- Tabuladores y colores se configuran para la sesión; no hay configuración
  persistente. La impresión permanece desactivada.
- Los documentos de más de 96 KiB usan almacenamiento temporal en `TEMP`,
  después `TMP`, o en el directorio actual. El temporal puede crecer con las
  ediciones y no se compacta durante la sesión. No hay recuperación automática
  tras un cierre forzado.
- El guardado prepara un temporal y un respaldo en el directorio del destino.
  Comprueba errores de escritura, cierre y renombrado; conserva los cambios
  pendientes cuando falla. No garantiza durabilidad ante corte eléctrico,
  conservación de toda la metadata ni coordinación con escritores simultáneos.

## Origen y averiguaciones

Raúl Sanguiao (velorek1) propuso el experimento y aportó `edit.dec`: una
representación de los bytes del ejecutable mediante enteros decimales con signo,
separados por puntos. Javier Sanguiao coordinó las peticiones y entregas. El
análisis y las pruebas de las versiones anteriores guiaron la recreación.

Los registros conservados de ese trabajo establecen:

1. Validar el rango −128…127 y aplicar `valor & 0xff` reconstruye un ejecutable
   MZ de **69.886 bytes**. Su SHA-256 es
   `68047c8ede4ee93cf704bdcc5d97caa3d5a5a787726d87cb39d4db9f25fa67a8`.
2. Los recursos identifican MS-DOS Editor 2.0.026, copyright Microsoft de 1995
   y un runtime Microsoft de 1992. Esto no identifica la versión exacta del
   compilador. Borland C++ 3.1 fue un objetivo de la antigua recreación DOS,
   no un compilador demostrado para el original.
3. El desempaquetado EXEPACK produce una imagen de **62.480 bytes**, con
   **40 bloques** y **14 reubicaciones**, y entrada relativa **0000:0718**.
   La validación histórica ejecutó el stub original con Unicorn en tres bases
   de carga y comparó la imagen completa con el desempaquetador Python.
   Verificar el stub no equivale a verificar todo el editor.
4. Se localizaron **248 recursos**. Sus offsets son relativos a cada entrada
   de la tabla, no a una base común. Los menús, textos y controles ayudaron a
   entender las funciones y el aspecto del programa.
5. El arranque y una rutina candidata a `main` se analizaron con pseudocódigo.
   Los nombres inferidos son hipótesis; no se recuperaron todos los algoritmos
   ni símbolos originales.

La documentación histórica resumida está en [docs/ORIGIN.md](docs/ORIGIN.md).
Este paquete no incluye el ejecutable de Microsoft, `edit.dec`, imágenes DOS,
compiladores antiguos ni volcados de código máquina. Compilar y probar esta
versión es independiente del material de referencia.

## Decisiones técnicas

El núcleo utiliza un **gap buffer** para documentos pequeños. Para los mayores
combina un archivo temporal, una lista de fragmentos y una caché de 4 KiB. Son
decisiones nuevas, no estructuras demostradas en el original. Las vistas
comparten documento pero conservan sus posiciones de cursor y desplazamiento.

El backend Linux usa `termios`, pseudo-eventos de teclado/ratón y secuencias ANSI.
Redibuja las celdas que cambian y adapta la pantalla al tamaño del terminal.
Se conserva CP437 para los caracteres heredados y la estética de la interfaz.

La versión 0.4 elimina `dos.c`, las ramas Borland, los punteros `huge`, las APIs
DOS de directorios y unidades, los scripts BAT y las herramientas de compilación
DOS. La ayuda es texto integrado en C y no busca `EDIT.HLP`. La salida con Escape
reutiliza la ruta de Alt+X y sus confirmaciones, sin duplicar la lógica de guardado.

| Archivo | Responsabilidad |
|---|---|
| `src/buffer.c` | Documento, almacenamiento, búsqueda y E/S |
| `src/editor.c` | Interacción, menús, ayuda, documentos y vistas |
| `src/host.c` | Terminal Linux, entrada, ratón y dibujo |
| `src/files.c` | Rutas, enumeración y filtros |
| `src/cli.c` | Argumentos de línea de comandos |
| `src/utf8.c` | Decodificación y conversión de caracteres |
| `tests/` | Pruebas C y de interfaz real mediante pseudo-terminal |

![Ayuda integrada a 75×24](docs/help.png)

## Autoría y créditos

**Kai es el nombre usado por el asistente LLM de OpenAI que generó el código
nuevo, las herramientas de análisis, las pruebas y la documentación del
proyecto**, bajo las instrucciones de Javier y las propuestas y pruebas de Raúl.
Esta preparación Linux también se ha realizado con ese asistente en Codex.
Kai no es una persona ni el autor del programa histórico de Microsoft. No se
atribuye un modelo concreto a sesiones anteriores sin un registro que lo pruebe.

Raúl aportó la idea, la referencia y comentarios de uso que orientaron los
cambios. Su proyecto C-EDIT es un trabajo independiente: esta entrega procede de
MS-EDIT 0.3.1, no de una modificación del repositorio C-EDIT.

Microsoft conserva la autoría del programa original. Este proyecto no está
patrocinado ni aprobado por Microsoft. Consulta [NOTICE.txt](NOTICE.txt).
No se ha elegido todavía una licencia de distribución para el código nuevo;
esta preparación no añade una licencia en nombre de los responsables humanos.

## Validación de esta entrega

Compilación con GCC 13.3.0 en Linux, C89 estricto, `-pedantic -Wall -Wextra
-Werror -O2`. Pasan **cinco ejecutables de pruebas C** y **26 pruebas Python** de
interfaz. Incluyen 8.000 ediciones aleatorias, documentos de 1 MiB, fallos de E/S,
UTF-8 español, búsquedas, reemplazo, selección, varias vistas, ratón y resize.
Las nuevas pruebas cubren la ayuda y Escape con guardar, cancelar o descartar.

Los detalles y límites de la comprobación están en
[docs/TESTING.md](docs/TESTING.md). No se afirma compatibilidad con todos los
emuladores de terminal ni equivalencia total con MS-DOS Edit.

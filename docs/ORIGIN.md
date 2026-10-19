# Procedencia del análisis

Esta entrega deriva del paquete `msedit-re-0.3.1-linux-20261012.zip` del trabajo
anterior de Javier y Raúl. La síntesis del README se ha cotejado con sus archivos
`analysis/manifest.json`, `analysis/stub-verification.json` y `docs/RE_NOTES.md`.
Los resultados de ingeniería inversa se describen como históricos: no son nuevas
pruebas de la versión Linux ni una afirmación de haber recuperado todo el fuente.

Se incluyen dos registros históricos reducidos a datos y resultados en
`research-manifest.json` y `stub-verification.json`. No contienen el ejecutable
ni permiten repetir el desempaquetado sin la referencia original. Las herramientas
DOS y sus pruebas ya no forman parte del código de esta versión.

El análisis inicial distinguió reconstrucción de bytes, desempaquetado EXEPACK,
extracción de recursos e implementación nueva. El código del editor no es una
traducción literal completa del ejecutable. Las estructuras del búfer, las vistas
y la capa Linux fueron diseñadas para esta implementación.

La limpieza 0.4 no intenta cambiar las convenciones de guardado ni completar
Unicode, deshacer, impresión o persistencia. Sus cambios funcionales son ayuda
integrada y Escape para salir, además de retirar opciones y convenciones de
rutas exclusivas de DOS.

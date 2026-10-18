# Documentación de RAVE Extended (`feature/extended`)

Bienvenido a la documentación técnica de las modificaciones y extensiones añadidas en la rama `feature/extended`.

Esta versión extiende **RAVE** (*RISC-V Automated Vector Emulator*) con capacidades avanzadas de clasificación de instrucciones vectoriales, análisis dinámico de riesgos y dependencias de datos (RAW, WAR, WAW), cálculo de nuevas métricas arquitecturales...

---

## Secciones de la Documentación

1. **[Arquitectura del Perfilador](arquitectura/index.md)**
   - [Clasificación Extendida de Instrucciones (`type_ext`)](arquitectura/clasificacion-instrucciones.md): Desglose del nuevo sistema de tipado de 16 bits para operaciones aritméticas (widening, narrowing, fused, reductions, mask, permutations) y de memoria (segmentadas, indexadas ordenadas/desordenadas).
   - [Rastreo de Riesgos y Dependencias de Datos](arquitectura/rastreo-dependencias.md): Detección en ventana configurable de dependencias RAW, WAR y WAW con desglose escalar/vectorial sobre 96 registros arquitecturales.
   - [Métricas Vectoriales y Estadísticas](arquitectura/metricas-vectoriales.md): Promedios de VL, LMUL efectivo (incluyendo LMULs fraccionales), porcentaje de ocupación respecto a VLMAX, afectación de políticas `ta`/`tu` y desviación estándar de strides.

2. **[Guía de Uso](guia-uso/index.md)**
   - [Configuración del Entorno](guia-uso/configuracion-entorno.md): Uso del script [`entorno.sh`](file:///home/jabautis/Escritorio/TFG/rave-ext/entorno.sh), flags de ejecución y configuración de QEMU.

---

## Resumen Ejecutivo: `main` vs `feature/extended`

| Característica | Rama `main` | Rama `feature/extended` |
| :--- | :--- | :--- |
| **Clasificación Aritmética** | Básica (FP / INT, Fused genérico). | Jerárquica: Widening, Narrowing, Normal (Fused, Reducciones FP/INT, Máscara, Permutaciones: Moves, Slides, Gather, Compress). |
| **Clasificación de Memoria** | Unit, Strided, Indexed, Spill. | Añade cargas/almacenamientos segmentados (unit, strided, indexed), e indexed ordenados vs desordenados. |
| **Transferencias Escalar-Vector** | No diferenciadas en el mix. | Contabiliza lecturas/escrituras de registros escalares y movimientos de registro completo (*whole-register*). |
| **Rastreo de Riesgos (Hazards)** | Inexistente. | Rastreo completo de **RAW**, **WAR** y **WAW** con ventana configurable (`RAVE_RAW_DIST`, `RAVE_WAR_DIST`, `RAVE_WAW_DIST`) y cálculo de distancia media. |
| **Espacio de Registros Monitorizado** | Solo para formato Paraver. | Mapeo continuo de **96 registros** (32 enteros `x`, 32 flotantes `f`, 32 vectoriales `v`). |
| **Métricas de Configuración Vectorial** | VL medio básico. | Avg VL, Avg LMUL (soporta fraccionales 1/2, 1/4, 1/8), % de ocupación respecto a VLMAX, y conteo de impacto `ta`/`tu`. |
| **Estadísticas de Stride** | Promedio de stride en bytes. | Promedio y **Desviación Estándar / Varianza** del stride en bytes. |
| **Uso de Registros Vectoriales** | No disponible en el informe estándar. | Registro del mapa de bits de uso individual de los 32 registros vectoriales (`v0`..`v31`). |

# Configuración del Entorno y Compilación

En este documento se describen los pasos necesarios para preparar el entorno de trabajo para poder usar rave-extended.

---

## 1. Variables de Entorno (`entorno.sh`)

En la raíz del repositorio se proporciona el script [`entorno.sh`](../../entorno.sh), que contiene las variables recomendadas para habilitar el perfilado extendido:

```bash
# Habilita el registro de instrucciones escalares, sin el, tampoco registraremos las dependencias entre instrucciones vectoriales y escalares
export RAVE_TRACE_SCALAR=1

# Habilita el informe estadístico en consola
export RAVE_PRINT_REPORT=1

# Define VLEN
export RAVE_VLEN=16384

# ACTIVA EL MODO EXTENDIDO: clasificación 16-bit y métricas avanzadas. Poner a 0 para compilar.
export RAVE_TRACE_EXTENDED=1

# Controla la emisión de la traza dinámica por instrucción en formato JSONL
export RAVE_INST_TRACE_ENABLED=0
export RAVE_INST_TRACE_NAME=rave_inst_trace.jsonl

# Permite fijar la distancia máxima entre instrucciones para detectar dependencias
export RAVE_RAW_DIST=10
export RAVE_WAR_DIST=10
export RAVE_WAW_DIST=10

# Modo de depuración (mantener a 0 por defecto)
export RAVE_DEBUG_INFO=0
```

### Aplicar la Configuración
Ejecuta siempre este comando en tu sesión de terminal antes de emular:

```bash
source entorno.sh
```
**Es importante que RAVE_TRACE_EXTENDED=0 al compilar.**

---

## 2. Compilación del Plugin RAVE

En la rama `feature/extended`, el archivo [`makefiles/make_RAVE.mk`](../../makefiles/make_RAVE.mk#L22) añade el nuevo objeto `inst_trace.o` a la lista de dependencias del plugin compartido `librave.so`.

Para compilar RAVE:

```bash
# Compilar para RVV 1.0 (predeterminado)
# Si lo has compilado con anterioridad y has modificado algún .h
rm -rf build/qemu-rave rave_sources/obj && make
# Sino únicamente haz:
make
```

---

## 3. Scripts de Ejecución (`rave-1_0.sh` y `rave-0_7_1.sh`)

Los scripts de ejecución ubicados en [`rave_sources/scripts/rvv/`](../../rave_sources/scripts/rvv/) han sido actualizados en `feature/extended` para reenviar automáticamente todas las nuevas variables de entorno como argumentos de plugin a QEMU:

- **Para RVV 1.0:** [`rave_sources/scripts/rvv/rave-1_0.sh`](../../rave_sources/scripts/rvv/rave-1_0.sh)
- **Para RVV 0.7.1:** [`rave_sources/scripts/rvv/rave-0_7_1.sh`](../../rave_sources/scripts/rvv/rave-0_7_1.sh)

Ambos scripts traducen las variables del entorno a la sintaxis esperada por QEMU (`-plugin ...`).
### Sintaxis Básica de Ejecución
```bash
./rave_sources/scripts/rvv/rave-1_0.sh ./tu_binario_riscv 
```

import sys
import json
import argparse
from collections import Counter

def main():
    # Configurar el analizador de argumentos
    parser = argparse.ArgumentParser(description="Clasifica y cuenta instrucciones desde un archivo JSONL.")
    parser.add_argument("archivo", help="Ruta al archivo .json a procesar")
    parser.add_argument("-s", "--sort", action="store_true", help="Ordena la salida de mayor a menor cantidad")
    
    # Parsear los argumentos introducidos por el usuario
    args = parser.parse_args()

    archivo_json = args.archivo
    contador_ops = Counter()

    try:
        with open(archivo_json, 'r', encoding='utf-8') as f:
            for linea in f:
                linea = linea.strip()
                if not linea:
                    continue
                
                try:
                    registro = json.loads(linea)
                    # Comprobar si el registro contiene la clave 'op'
                    if 'op' in registro:
                        contador_ops[registro['op']] += 1
                except json.JSONDecodeError:
                    print(f"Aviso: Línea no válida ignorada -> {linea}")
                    
    except FileNotFoundError:
        print(f"Error: No se ha encontrado el archivo '{archivo_json}'.")
        sys.exit(1)

    # Si se pasó el flag -s, usamos most_common() para ordenar de mayor a menor. 
    # Si no, iteramos normalmente con items()
    if args.sort:
        elementos = contador_ops.most_common()
    else:
        elementos = contador_ops.items()

    # Formatear la salida
    resultados = [f"{cantidad}: {op}: " for op, cantidad in elementos]
    
    # Imprimir el resultado final separado por saltos de línea
    print("\n".join(resultados))

if __name__ == "__main__":
    main()
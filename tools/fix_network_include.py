# Arduino core 3.3.x (espressif32 8.0.0): WiFi / AsyncTCP incluyen "Network.h" y "NetworkInterface.h",
# que estan en la libreria "Networking" del core, pero el LDF de PlatformIO no siempre anade su ruta.
# Este script localiza la carpeta que contiene Network.h dentro del framework y la anade a CPPPATH
# (sin depender del nombre de la carpeta: Network, Networking...). En cores 2.x no hace nada.
import os
Import("env")

try:
    fw = env.PioPlatform().get_package_dir("framework-arduinoespressif32")
except Exception:
    fw = None

found = []
if fw:
    libs = os.path.join(fw, "libraries")
    for root, dirs, files in os.walk(libs):
        if "Network.h" in files and "NetworkInterface.h" in files:
            found.append(root)
            dirs[:] = []
    for p in found:
        env.Append(CPPPATH=[p])
        print("fix_network_include: anadida ruta", p)
    if not found:
        print("fix_network_include: Network.h no encontrado en", libs, "(core 2.x o estructura distinta)")

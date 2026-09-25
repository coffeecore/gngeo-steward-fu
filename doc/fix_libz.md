On passe au warning `libz.so.1`. Il est indépendant de GnGeo lui-même et vient du fait que le binaire est lié avec la zlib du sysroot, mais qu’en runtime il prend `/usr/lib/libz.so.1` du firmware.

Dans `make/gngeo.mk`, juste après la copie de `libts`, ajoute :

```make
	@test -e "$(SYSROOT)/usr/lib/libz.so.1"
	cp -L "$(SYSROOT)/usr/lib/libz.so.1" \
		"$(GNGEO_PAK)/lib/libz.so.1"
```

Donc ton bloc d’installation devient en gros :

```make
	# The Buildroot SDL used by GnGeo has a runtime dependency on tslib.
	@test -e "$(SYSROOT)/usr/lib/libts-1.0.so.0"
	mkdir -p "$(GNGEO_PAK)/lib"
	cp -L "$(SYSROOT)/usr/lib/libts-1.0.so.0" \
		"$(GNGEO_PAK)/lib/libts-1.0.so.0"

	@if [ -d "$(SYSROOT)/usr/lib/ts" ]; then \
		cp -aL "$(SYSROOT)/usr/lib/ts" "$(GNGEO_PAK)/lib/"; \
	fi

	@test -e "$(SYSROOT)/usr/lib/libz.so.1"
	cp -L "$(SYSROOT)/usr/lib/libz.so.1" \
		"$(GNGEO_PAK)/lib/libz.so.1"
```

Ton `install-gngeo` crée déjà `$(GNGEO_PAK)/lib` et y copie les dépendances privées. 

Ensuite :

```bash
make gngeo
```

Puis avant de copier sur la console, vérifie :

```bash
file output/gngeo/NEOGEO.pak/lib/libz.so.1
ls -lh output/gngeo/NEOGEO.pak/lib/libz.so.1
```

Il faut obtenir un vrai ELF ARM, pas un lien symbolique ni un petit fichier texte.

Sur la console, relance `mslug.gno` ou `mslug.zip`. Le début du log ne devrait plus contenir :

```text
/usr/lib/libz.so.1: no version information available
```

Comme ton `launch.sh` met déjà :

```sh
export LD_LIBRARY_PATH="$EMU_DIR/lib:/mnt/SDCARD/System/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
```

le loader prendra automatiquement :

```text
Neo Geo.pak/lib/libz.so.1
```

avant celle du firmware.

Fais cette modif + `make gngeo`, puis donne-moi juste le début du nouveau log.

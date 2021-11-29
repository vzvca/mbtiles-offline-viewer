# OpenMapTiles standalone viewer

This repo contains small standalone C programs to display maps using maplibre in offline / air-gapped environments.

Subdirectory `stage1` contains source code for a small C program which can render the world countries offline.
This program is a refactor in C of https://github.com/klokantech/mapbox-gl-js-offline-example.

Subdirectory stage2 contains an offline mbtiles viewer. It is derived from program in stage1.

All programs start a local web server and (on Linux) opens the web browser to display the map using `xdg-open`.

I wrote them to understand how to use `maplibre` and how to build standalone applications using it. *This is not production grade software* but maybe they can be useful as simple examples.

## Stage 1 : world map view

Program is build using `make`, the output executable is `world`. Start it directly typing `./world -p 9999 -x` which will start a web server listening on port 9999 and a web browser pointing to URL `http://127.0.0.1:9999`.

The `site` subdirectory contains the whole web site tree. All the web site which includes HTML, CSS, Javascript, fonts and map tiles gets bundled in the final executable which does not have any dependencies. The program `world` can be copied and run onto another machine provided it runs Linux on the same architecture.

## Stage 2 : mbtiles viewer

The program is designed to display *vectorial* `mbtiles` files. It is easier to display `mbtiles` files from `openmaptiles` since the program comes with 4 (slightly modified) openmaptiles rendering styles bundled inside (basic, bright, dark and positron). It is still possible to display another `mbtiles` file providing your own `style.json` file, an example of this is available in `data/ex1`.

Program is build using `make`, the output executable is `mbv`. Start it typing `./mbv -p 9999 -x -m data/ex2/iceland.mbtiles -s @bright` which will start a web server listening on port 9999 and a web browser pointing to URL `http://127.0.0.1:9999`.

The `site` subdirectory contains the whole web site tree. All the web site which includes HTML, CSS, Javascript, fonts gets bundled in the final executable which does not have any dependencies. The program `world` can be copied and run onto another machine provided it runs Linux on the same architecture.

### Example 1 : world mbtiles

This example uses data from https://github.com/klokantech/vector-tiles-sample.

````
$ ./mbv -x -p 9999 -m ./data/ex1/countries.mbtiles -s ./data/ex1/style.json
````

### Example 2 : iceland mbtiles

This example uses data from 


## Credits

This project depends on work by other people.

http-parser https://github.com/nodejs/http-parser

mapbox-gl-js-offline-example https://github.com/klokantech/mapbox-gl-js-offline-example

vector-tiles-sample https://github.com/klokantech/vector-tiles-sample


## Licensing

As said in previous section this project depends on others people work which get included (for data) and statically linked (for code) in the executables.
As a consequence I believe "MIT license" can be applied to this project since :

* http_parser has "MIT license"
* mapbox-gl-js-offline-example same as vector-tiles-sample

`mbtiles` samples are not linked or bundled in the executables.

Let me know if something's wrong there ?



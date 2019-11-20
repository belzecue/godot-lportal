# godot-lportal
Portal rendering / Occlusion culling module for Godot 3.2\
You can now also use LPortal in single room mode to speed up culling in levels that have not been divided into rooms / portals!\
Note that this is a _Work in progress_. It is recommended to wait for a first release.

![plane_lines](images/lportal_boxrooms3.jpg)

Video of initial testing:\
https://www.youtube.com/watch?v=uVwLltiouBs \
https://www.youtube.com/watch?v=xF_3Fe2HRdk \
https://www.youtube.com/watch?v=NmlWkkhGoJA

### Demos / Tutorials
https://github.com/lawnjelly/lportal-demos

_Feel free to leave suggestions / feature requests on the issue tracker, especially regarding ease of use._

## Current status
The first demo tutorial (see above) is now working with the internal godot baked lightmap workflow. It shows registering dynamic objects (DOBs), loading and unloading levels.

Dynamic lights are now working, both global (directional lights) and local (omnis and spotlights):\
https://www.youtube.com/watch?v=1WT5AXZlsDc \

I have just added a single room mode, which allows LPortal to speed up culling in games that haven't even been designed as rooms and portals.


## Roadmap
* Auto conversion of named room spatials and portal mesh instances to LRoom and LPortal DONE
* Auto creation of mirror portals DONE
* Recursive determine visibility DONE
* Prevent memory allocations (use pools for plane vectors) DONE
* Add support for objects moving between rooms - cameras, players, physics etc - DONE
* Refactor code, moving LRooms and LPortals outside scene graph DONE
* Cleanup code, Optimize DONE
* Handle special cases (multiple portals views into room etc) DONE
* Optimize non-moving statics DONE
* Optional convex hull bound for rooms DONE
* Add debug graphical view of portal planes DONE
* Add debug graphical view of room bounds DONE
* Dealing with shadows from objects outside of view PARTIALLY DONE
* Universal visibility query for camera and dynamic lights DONE
* Support for global directional lights (like the sun) ONGOING
* Building baked lightmap workflow ONGOING
* Bug fixing / testing ONGOING

* Shadow caster optimization
* Closable portals
* PVS (primary and secondary)

## Instructions
* [INSTRUCTIONS.md](INSTRUCTIONS.md)
* [INSTRUCTIONS_SINGLEROOM.md](INSTRUCTIONS_SINGLEROOM.md)
* [TUTORIAL](https://github.com/lawnjelly/lportal-demos/tree/master/Tutorial-Simple)

## Installation
You will need to compile Godot from source (for now). See:
http://docs.godotengine.org/en/3.0/development/compiling/index.html

Once the engine is compiling okay on your system, to add the module:
* Create a folder inside godot/modules called 'lportal'
* Clone / download this repository as a zip file and place the files in the lportal folder
* Compile the engine as normal, it should automatically pick up the lportal module
* Note that to export to other platforms you will also have to compile export templates for those platforms

You will know the installation was successful when you see a new Node type 'LRoomManager' in the Godot IDE.

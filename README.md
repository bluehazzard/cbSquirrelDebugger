# cbSquirrelDebugger
A squirrel debugger plugin for code::blocks

## What it does:
This plugin provides a user interface to the SQDBG (http://wiki.squirrel-lang.org/files/sqdbg_3.x_07_May_2011.zip) debugger for the squirrel scripting language (http://squirrel-lang.org/). It alows you to use c::b as a visual squirrel debugger.

Implemented features:
* Connect to SQDBG over network (the file structure has to match so that c::b can find the files)
* Open files, as loaded from the debugger (only with the specific c::b implementation of SQDBG, found in the source tree of c::b)
* Breakpoints
* Standard debug features like "run to cursor","step into","step out" etc.
* Automatic local watches
* User defined watches
* Tooltip highlighting of watches
* Backtrace with stack switching

## What do you need:
1. a c::b test version with the new scripting capabilities. You can find it here: https://github.com/bluehazzard/codeblocks_sf . You have to compile it for yourself. A instruction how to compile c::b can be found on the official c::b wiki
2. this repository ;)

## How to install:
1. compile c::b
2. compile this plugin (use the provided project files)
3. run target "default" and the plugin will get installed in your c::b directory (specified by Settings->Global Variables->Current Variable = cb. The base path is mandatory )
4. run target "cbplugin" and a codeblocks plugin archive will be created in the project folder. This can then be installed over Plugins->Manage plugins->Install new


## How to use:
1. Install the plugin in c::b
2. Under Settings->Debugger->Squirrel Debugger set ip address and port of your server
3. Open a project (can be a squirrel project, or any dummy project)
4. Hit debug (red arrow)
5. You can now use the debugger like any other debugger in c::b

## Use the plugin to debug c::b squirrel plugins, or code run by c::b
1. You need the new scripting version of c::b (https://github.com/bluehazzard/codeblocks_sf)
2. You will need two instances of c::b the first will act as client and debugger and the second instance will run the script and act as server
3. Start the server instance with the command line "-sdbg=5351:h". c::b will start and stop with a dialog, waiting for the client to connect
4. Start the second c::b (client) with this plugin installed.
5. Open some temporary project.
6. Hit the debug button (red arrow)
7. The first instance of c::b (server) will now start
8. All script files loaded from the server will open in the debugger (absolute paths are used). If a script is loaded form a zip file (like script plugins) a temporary file will be created and loaded in c::b
9. Now you can set breakpoints and watches and debug the whole stuff...
10. To stop hit "break"


## Be aware:
* This is the earliest version of this plugin and also the c::b version is not fully tested. There can sleep a lot bugs and ui errors under the hood. If you find some, please report them.

## Tanks to (no specific order)
fagiano (squirrel autor)
wizzard (sqrat)
alpha   (python debugger for c::b, i used it as template)
and all others...


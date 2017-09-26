<!-- NOTE: please fill in the first section with information about your game. -->

# *Robot Fun Police*

*Robot Fun Police* is *Jia Zheng*'s implementation of [*Robot Fun Police*](http://graphics.cs.cmu.edu/courses/15-466-f17/game2-designs/jmccann/) for game2 in 15-466-f17.

![Game Screenshot](./screenshots/ss1.png)

## Asset Pipeline

The assets used are the ones provided in the `robot.blend` file. Data were retrieved using the modified `export-meshes.py` Python script which grabs mesh information such as vertices, surface normals and colours as well as scene information, which were then exported into binary files. We load these files in the application.

## Architecture

Most of the models were loaded automatically, with the exception of the balloons and the robot. To simulate the hierarchy of the robot parts, we use a stack structure (represented with a vector) to store the different components of the robot. While the game is running and user input is recorded, the resulting transformation of the bottom-most robot part was perpetuated into its linked child components. We do the same for all child joints. Then, we calculate the displacement between the tip of the robot pin and the center of the balloons. We assume that the balloons are perfectly spherical to facilitate computation, and we "pop" a balloon if the tip of the robot arm lies within the bounds of the balloon.

## Reflection

The main difficulty of the assignment lies in computing the transformation matrix of the individual parts after a rotation. I had difficulties in visualising the axes of rotation for each individual joint of the robot. The base of the robot rotates around its z-plane, and all the other links around their own x-plane. Nevertheless, I was perplexed on how to aggregate the rotations altogether. Additionally, as with the previous assignment, I was unsure of how to "remove" a model from the scene rendered. 

The design document was clear and I had no problems with it.


# About Base2

This game is based on Base2, starter code for game2 in the 15-466-f17 course. It was developed by Jim McCann, and is released into the public domain.

## Requirements

 - modern C++ compiler
 - glm
 - libSDL2
 - libpng
 - blender (for mesh export script)

On Linux or OSX these requirements should be available from your package manager without too much hassle.

## Building

This code has been set up to be built with [FT jam](https://www.freetype.org/jam/).

### Getting Jam

For more information on Jam, see the [Jam Documentation](https://www.perforce.com/documentation/jam-documentation) page at Perforce, which includes both reference documentation and a getting started guide.

On unixish OSs, Jam is available from your package manager:
```
	brew install ftjam #on OSX
	apt get ftjam #on Debian-ish Linux
```

On Windows, you can get a binary [from sourceforge](https://sourceforge.net/projects/freetype/files/ftjam/2.5.2/ftjam-2.5.2-win32.zip/download),
and put it somewhere in your `%PATH%`.
(Possibly: also set the `JAM_TOOLSET` variable to `VISUALC`.)

### Bulding
Open a terminal (on windows, a Visual Studio Command Prompt), change to this directory, and type:
```
	jam
```

### Building (local libs)

Depending on your OSX, clone 
[kit-libs-linux](https://github.com/ixchow/kit-libs-linux),
[kit-libs-osx](https://github.com/ixchow/kit-libs-osx),
or [kit-libs-win](https://github.com/ixchow/kit-libs-win)
as a subdirectory of the current directory.

The Jamfile sets up library and header search paths such that local libraries will be preferred over system libraries.

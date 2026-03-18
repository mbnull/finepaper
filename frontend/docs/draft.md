# Main goals
Write a IP configuration frontend under frontend.
The total workflow is the first image.
The configured IR current is a json file ,but format is not defined yet.Use the framework/example scheme to as first stage target.

# First step
Should  can display topology correctly, and assume only mesh topology is supported. Every endpoint can be config while generate.The endpoint current is not have many config, you can reference framework scheme.

Each create, config will use a create wizzard, and current assume current version only support noc.

It ***MUST*** can generate json for framework t use.The framework is not complete yet, so how to call lib current is just use generate script.

# Second Step
add ports for all componenets. The target is second image(example image).
TBD.

# Notes
we will use xmake as build system. you can change all files under frontend. and it provide a simple template under frontend/qt. The xmake doc is under frontend/docs/xmake.txt

All reference image is below.
![alt text](image-1.png) after upload ,is 1.jpeg or 2.jpeg

![example image](image.png) after upload is 3.jpeg

![example 2](socreates.png) after upload is 4.jpeg
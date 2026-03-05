# CommRaT Messages
I like the idea of ros, to provide message collections as repos/packages.

## Music Msgs
I would love to start with realtime music hardware, so music_msgs would be great

## Message Generation and Compatibility 
If we create similar cmake instructions to ros, we could provide nice generic compatibility.
I would love to use sertial inspect for that as a nice gui tool. We can then create a similar structure.
We should have full compatibility except for arrays[] for SeRTial compatibility we just need to provide a max length addition and make it configurable per-project. it should be easy to utilize inspect mechanisms to just show whats missing for realtime compatibility 

Messages like 
```
example_msg
name string
value int
data float[]
```
are compatible except from max compile time sizes. but thats easy, sertial compatibility:
```
name string[kMaxLen]
value int
data float[kMaxVals]
```
is SeRTial compatible

and with inspect we could ask, when setting up projects

```bash
rtify example_msg
name max len?: 40
data max size?: 4096
project fully rtified
```

thats it!
of course this can be done recursively for all ROS messages of a project to rtify the project

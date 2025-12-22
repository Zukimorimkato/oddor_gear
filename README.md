This is the driver for H pattern shifter from [ZSC ODDOR](aliexpress.com/item/1005007543763824.html)  "vibecoded" to function in something like BeamNG under Linux. 

It is janky. It just works on my system. Some basic checks for safety and all that were done through LLM as well. 

Inside you'll find everything you need to install. 

Just clone the repo, chmod all *.sh files. Run them. They will ask for you sudo password. You also might need to change the VENDOR/PRODUCT IDs so it would function.

Script installs it as dkms module, might break with kernel update, don't forget to reboot. You can test using evtest ot jstest. 

If you want make PR, maybe I'll test them and it will be a better thing to use or don't =)

IMPORTANT! Once again. It is janky and most likely not okay to be used. 

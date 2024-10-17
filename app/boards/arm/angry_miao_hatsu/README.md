# Create Docker Image
> cd ./zmk/.devcontainer/

> docker build -t zmk:3.5 .

## Delete previous module
**From local machine**

> rm -rf modules && rm -rf .west

## Create the container 
**From zmk root folder**

> docker run -ti --rm -v `pwd`:/opt/zmk zmk:3.5

# Init west 
**From the container**

> west init -l /opt/zmk/app/

> cd /opt/zmk && west update

# Start building
**From the container**

> cd /opt/zmk/app/

> west build -d build/left -b angry_miao_hatsu_left

> west build -d build/right -b angry_miao_hatsu_right

# Build output
> /opt/zmk/app/build/left/zephyr/zmk.uf2

> /opt/zmk/app/build/right/zephyr/zmk.uf2

# Flash
- disconnect batteries of left/right
- plug the usb and dongle to flash the right halve and disconnect the usb
- plug the usb and dongle to flash the left halve, without rebooting plug the battery and disconnect the usb
- connect the battery to the right halve

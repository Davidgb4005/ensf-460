import lgpio
for chip in [0, 10, 11, 12, 13]:
    try:
        h = lgpio.gpiochip_open(chip)
        print(opened, chip, handle, h)
        lgpio.gpiochip_close(h)
    except Exception as e:
        print("failed", chip, repr(e))

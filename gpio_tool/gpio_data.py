import os
import json
class plat_config:
    config_data = None
    select_plat = "N10"
    def __init__(self):
        self.current_file_path = os.getcwd()
        self.gpio_rev_file_path = "config/gpio_resource.json5"
        self.gpio_config_data_plat = os.path.join(self.current_file_path, self.gpio_rev_file_path )
        print(self.gpio_config_data_plat)
        with open(self.gpio_config_data_plat, 'r') as fconfig:
           self.config_data = json.load(fconfig)
        print(self.config_data)

if __name__  == '__main__':
    plat = plat_config()
# xc_hawk
Earth magnet field assisted HAWK sensor with EPS32-S3 CPU

- Clone and install ESP IDF
mkdir -p ~/esp; cd ~/esp; git clone -b release/v5.4 --recursive https://github.com/espressif/esp-idf.git;
./install.sh; . ./export.sh;
pip install cmake;
git submodule update --init --recursive;
idf.py set-target esp32-s3

( This will get the stable branch release/v5.4 of esp-idf plus corresponding compiler, etc. For the docker official releases, branch label is release-v5.4, see also workflow yaml. )

- Compile the hello_word application under ~/esp-idf/examples/get-started/hello_world/ to validate EPS-IDF installation

- If hello_word works, clone and compile this repo:
cd ~/esp/esp-idf/examples/get-started/; git clone https://github.com/iltis42/xc_hawk.git
cd xc_hawk;
idf.py flash -p /dev/ttyACM0;
idf.py monitor -p /dev/ttyACM0;

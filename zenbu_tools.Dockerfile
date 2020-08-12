FROM debian:9

LABEL description="Command-line tools for the ZENBU genome browser"

RUN apt update && apt -y install build-essential
RUN apt -y install git
RUN git clone https://github.com/jessica-severin/ZENBU
RUN cd /ZENBU && git checkout 2.11.2
RUN apt -y install zlib1g-dev libncurses-dev libsqlite3-dev \
                   libboost-dev libssl-dev default-libmysqlclient-dev \
                   libcurl4-openssl-dev libfcgi-dev
RUN cd /ZENBU/c++ && make && cd tools && make && make install
RUN rm -rf /ZENBU
RUN apt -y purge build-essential && apt -y autoremove
RUN apt -y purge libboost-dev && apt -y autoremove
RUN apt -y purge zlib1g-dev libncurses-dev libsqlite3-dev \
                 libboost-dev libssl-dev default-libmysqlclient-dev \
                 libcurl4-openssl-dev libfcgi-dev
RUN apt clean

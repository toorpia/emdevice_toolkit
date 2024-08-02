#!/bin/bash

export PATH=/usr/bin:/bin:${PATH}
WORK_DIR=${HOME}/work

echo -n "Start: houseKeeping: "
date +%Y%m%d%H%M%S

find ${WORK_DIR}/rawdata -type f -ctime +1 | grep '\.wav$' | xargs /bin/rm -v
if [ -d ${WORK_DIR}/rawdata.non-effective ]; then
    find ${WORK_DIR}/rawdata.non-effective -type f -ctime +7 | xargs /bin/rm -v
fi

echo -n "Complete: houseKeeping: "
date +%Y%m%d%H%M%S
echo "==============================="

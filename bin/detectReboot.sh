#!/bin/bash

file_to_record_alivetime="${HOME}/recorded_alivetime"

if [ -e ${file_to_record_alivetime} ]
then
    last_recorded_alivetime=`/bin/ls -l --time-style='+%s'  ${file_to_record_alivetime}  |  /usr/bin/cut -d" " -f 6`
else
    touch ${file_to_record_alivetime}
    exit 0
fi

last_boottime=`uptime -s | xargs -I{} date -d "{}" +%s`

if [ ${last_recorded_alivetime} -lt ${last_boottime} ]; then
    reboot_time=$(date -d @${last_boottime})

    #echo "THIS IS TEST"
    echo "CAUTION: REBOOT DETECTED: ${HOSTNAME}: ${reboot_time}"

    touch ${file_to_record_alivetime}
fi

exit 0

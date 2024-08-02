#!/bin/bash

SLACK_WEBHOOK_URL="https://hooks.slack.com/services/[replace your webhool url]"

MESSAGE=`cat $*`
DATE=`date +"%Y-%m-%d %H:%M:%S %Z"`

if [ "${MESSAGE}" != "" ]; then
    curl -s -X POST --data-urlencode "payload={'text': '<@UD4E23XPT> \"${MESSAGE}\" on host:${HOSTNAME} at ${DATE}'}" ${SLACK_WEBHOOK_URL} > /dev/null
fi

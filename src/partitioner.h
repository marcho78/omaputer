#ifndef __PARTITIONER_H
#define __PARTITIONER_H

#include <Arduino.h>

void partList();

void dumpPartition(const char *partitionLabel, const char *outputPath);

void restorePartition(const char *partitionLabel);

bool attachPartition(const String &from, String to);

void partitionCrawler();

// Shows `message` with Confirm/Cancel options; true if the user confirmed.
bool confirmAction(const String &message);

#endif /*__PARTITIONER_H*/

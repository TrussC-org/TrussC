#pragma once

// Private to this owned test binary; these are not framework API.
void metalGrowthReserve(int bytes);
void metalGrowthForceSplits(int count);
int metalGrowthSplitCount();
bool metalGrowthSlotsCaughtUp();
bool metalGrowthFailureChecks(sg_view attachment);
bool metalGrowthComputeCheck();

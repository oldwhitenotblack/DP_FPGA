#include "IlpAssign.h"
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <omp.h>
#include "time.h"
#include <math.h>
#include "gurobi_c++.h"
#include <string.h>
#include <algorithm>
#include<vector>
#include <chrono>
#include <iomanip>
#include <cassert>
#include <utility>
#include<iostream>
#include <stack>
#include <cfloat>
#include <set>
#include <unordered_set>
#include<unordered_map>
#include <utility>
#include <fstream>
#include<cstdio>
#include<assert.h>
#include"vector_add.cuh"
#include<sys/time.h>
#include <mutex>

using namespace std;
extern long total_blkcost;
extern long total_olpcost;
extern long total_wlcost;
extern long cost11,cost22,cost33;

extern int t_num;
extern int add_ins;
extern int segment_time;
extern int cost_time ;
extern int assign_time;
extern int update_time;
extern double totalIrouteCost1;

//可容忍最大subpanel规模
const short int upperbound_iroutenum = 2000;
//可容忍最大切割导线数ￄ1�7?
const short int upperbound_iroutecut = 1;

const double wl_cost_weight = 6;

const double blk_cost_weight = 500;

const double ol_cost_weight = 1;


ILPAssign::ILPAssign(TAdb* ta)
{
    this->taDB = ta;
}
int ILPAssign::GetBinCoord(int coord, int tile)
{
    int binCoord = tile * 10;
    binCoord = (coord / binCoord) * binCoord;
    return binCoord;
}
Coordinate3D ILPAssign::GetBinCoord_3D(Coordinate3D coord)
{
    Coordinate3D binCoord;
    binCoord.x = binCoord.y = 32*10;
    binCoord.x = (coord.x/binCoord.x)*binCoord.x;
    binCoord.y = (coord.y/binCoord.y)*binCoord.y;
    binCoord.layers = -1;
    return binCoord;
}


bool CmpStartSmallToLarge(Interval a, Interval b)
{
    return a.start < b.start;
}

bool CmpStart(Interval a, Interval b)
{
    return a.start < b.start; //small --> large
}



GRBLinExpr ILPAssign::GetCost(OverlapResult result)
{
//return /*(double)result.overlapCount*/(double)result.overlapLength + (double)result.overlapBlk*1000000;
//return (GRBLinExpr)result.overlapCount*(double)result.overlapLength*100000 + (double)result.overlapBlk*1000000;
    return (GRBLinExpr)result.overlapCount*(double)result.overlapLength+ (double)result.overlapBlk;
}

double ILPAssign::GetCost_NTA(OverlapResult result)
{
//return /*(double)result.overlapCount*/(double)result.overlapLength + (double)result.overlapBlk*1000000;
//return (double)result.overlapCount*(double)result.overlapLength*100000 + (double)result.overlapBlk*1000000;
    return  (double)result.overlapBlk*1000000;//(double)result.overlapCount*(double)result.overlapLength*100000+
}

OverlapResult ILPAssign::GetOverlapResult(OverlapResult result,Track track,Interval interval,Interval interval1,bool recordOverlapIroute)
{
    int olength;
    olength = min(interval.end , interval1.end) -
              max(interval.start , interval1.start);
    if(olength < 0)
        result.overlapLength = 0;
    return result;
}

OverlapResult ILPAssign::GetOverlapResult_NTA(Track track,Interval interval,bool recordOverlapIroute)//panel->tracks[k],panel->intervalList[j]
{
//sort(track.iroutes.begin(),track.iroutes.end(),CmpStart);

    Interval tempInterval;
    vector<Interval>::iterator intervalIt,intervalLow,intervalUp;

//[a, b] overlaps with [x, y] if b > x and a < y
    OverlapResult result;
    result.overlapCount = 0;
    result.overlapLength = 0;
    result.overlapBlk = 0;

//if (track.intervals.size() == 0)
//	return result;

//Condition a < y
    tempInterval.start = interval.end - 1;//interval[a,b]/tempInterval[x,y]/令a=y-1(即a<y)
//查找[begin,end]区域中第丢�个不符合CmpStart规则的元ￄ1�7?
    intervalUp = std::upper_bound(track.intervals.begin(),track.intervals.end(),tempInterval,CmpStart);//?

    for (intervalIt = track.intervals.begin(); intervalIt!=intervalUp; ++intervalIt)
    {
        if (interval.irouteIndex == intervalIt->irouteIndex)
            continue;

//Condition b > x
        if (intervalIt->end > interval.start)
        {
            ++result.overlapCount;
            result.overlapLength += min(intervalIt->end , interval.end) -
                                    max(intervalIt->start , interval.start);

            if (recordOverlapIroute)//?
                result.overlapIrouteIndex.push_back(intervalIt->irouteIndex);
        }
    }

//check overlap blockage
    int blkOverlap;
    for (int i=0;i<track.blk.size();++i)
    {
        blkOverlap = min(interval.end,track.blk[i].end)-
                     max(interval.start,track.blk[i].start);

        if (blkOverlap > 0)
            result.overlapBlk += blkOverlap;
    }

    return result;
}

OverlapResult ILPAssign::GetBlkResult(OverlapResult result,Track track,Interval interval, bool recordOverlapIroute)
{
//check overlap blockage
    int blkOverlap;
    for (int i=0;i<track.blk.size();++i)
    {
        blkOverlap = min(interval.end,track.blk[i].end)-
                     max(interval.start,track.blk[i].start);

        if (blkOverlap > 0)
            result.overlapBlk += blkOverlap;
    }
    return result;
}

//maintain irouteList of track are sort
//panel->tracks[minOverlapIndex],taDB->irouteList[irouteIndex],panel->intervalList[j]
void ILPAssign::AssignIrouteToTrack(Panel *panel,Subpanel *subpanel,GRBVar **x)
{
//bool assign = false;
    int irouteIndex;
    Iroute iroute;//&iroute
    Interval interval;
    for (int in = 0;in < subpanel->intervalList.size();in++){
        irouteIndex = subpanel->intervalList[in].irouteIndex;
        iroute = taDB->irouteList[irouteIndex];
        interval = subpanel->intervalList[in];
        for (int t = 0;t < panel->tracks.size();t++){
            if(x[in][t].get( GRB_DoubleAttr_X ) == 1){
                panel->tracks[t].intervals.insert(panel->tracks[t].intervals.begin(),interval);
                if(iroute.source.x == iroute.target.x)
                    taDB->irouteList[irouteIndex].source.x = taDB->irouteList[irouteIndex].target.x = panel->tracks[t].coordinate;
                else
                    taDB->irouteList[irouteIndex].source.y = taDB->irouteList[irouteIndex].target.y = panel->tracks[t].coordinate;
                if (iroute.subNetIndex != -1)
                    taDB->netList[iroute.netIndex].subNetList[iroute.subNetIndex].assigned = true;
                break;
            }
        }
    }
//sort iroutes by start
    for (int t = 0;t < panel->tracks.size();t++){
        sort(panel->tracks[t].intervals.begin(),panel->tracks[t].intervals.end(),CmpStartSmallToLarge);
    }
}
void ILPAssign::AssignIrouteToTrack_Cu(Panel *panel,int *x) {

//    int irouteIndex;
//    Iroute iroute;
//    Interval interval;
//    for(int in=0;in<panel->intervalList.size();in++){
//        irouteIndex = panel->intervalList[in].irouteIndex;
//        iroute = taDB->irouteList[irouteIndex];
//
//        interval = panel->intervalList[in];
//        int t = x[in];
//        panel->tracks[t].intervals.insert(panel->tracks[t].intervals.begin(),interval);
//        if(iroute.source.x == iroute.target.x){
//            taDB->irouteList[irouteIndex].source.x = taDB->irouteList[irouteIndex].target.x = panel->tracks[t].coordinate;
//        }
//        else{
//            taDB->irouteList[irouteIndex].source.y = taDB->irouteList[irouteIndex].target.y = panel->tracks[t].coordinate;
//        }
//        if (iroute.subNetIndex != -1)
//            taDB->netList[iroute.netIndex].subNetList[iroute.subNetIndex].assigned = true;
//    }
//    for (int t = 0;t < panel->tracks.size();t++){
//        sort(panel->tracks[t].intervals.begin(),panel->tracks[t].intervals.end(),CmpStartSmallToLarge);
//    }

    auto& intervalList = panel->intervalList;
    auto& irouteList = taDB->irouteList;
    auto& netList = taDB->netList;
    const int numTracks = panel->tracks.size();
    for(int in=0;in<intervalList.size();in++) {
        const Interval &interval = intervalList[in];
        const int irouteIndex = interval.irouteIndex;
        Iroute &iroute = irouteList[irouteIndex];
        int t = x[in];
        Track &track = panel->tracks[t];
        track.intervals.insert(track.intervals.begin(), interval);
    }
    for(int in=0;in<intervalList.size();in++){
        const Interval& interval = intervalList[in];
        const int irouteIndex = interval.irouteIndex;
        Iroute& iroute = irouteList[irouteIndex];
        int t = x[in];
        Track& track = panel->tracks[t];
        if(iroute.source.x == iroute.target.x) {
            iroute.source.x = iroute.target.x = track.coordinate;
        } else {
            iroute.source.y = iroute.target.y = track.coordinate;
        }
        if (iroute.subNetIndex != -1) {
            netList[iroute.netIndex].subNetList[iroute.subNetIndex].assigned = true;
        }
    }

    for (int t = 0; t < numTracks; t++) {
        // 使用局部变量缓存intervals
        auto& intervals = panel->tracks[t].intervals;
        std::sort(intervals.begin(), intervals.end(), CmpStartSmallToLarge);
    }
}
void ILPAssign::AssignIrouteToTrack_DP(Panel *panel,Subpanel *subpanel,vector<int> x)
{
//bool assign = false;

    int irouteIndex;
    Iroute iroute;//&iroute
    Interval interval;
    for (int in = 0;in < subpanel->intervalList.size();in++){
        irouteIndex = subpanel->intervalList[in].irouteIndex;
        iroute = taDB->irouteList[irouteIndex];
        interval = subpanel->intervalList[in];
        int t = x[in];
//        for (int t = 0;t < panel->tracks.size();t++){
//            if(x[in][t]==1){
        panel->tracks[t].intervals.insert(panel->tracks[t].intervals.begin(),interval);
        if(iroute.source.x == iroute.target.x)
            taDB->irouteList[irouteIndex].source.x = taDB->irouteList[irouteIndex].target.x = panel->tracks[t].coordinate;
        else
            taDB->irouteList[irouteIndex].source.y = taDB->irouteList[irouteIndex].target.y = panel->tracks[t].coordinate;
        if (iroute.subNetIndex != -1)
            taDB->netList[iroute.netIndex].subNetList[iroute.subNetIndex].assigned = true;
//                break;
//            }
//        }
    }
//sort iroutes by start
    for (int t = 0;t < panel->tracks.size();t++){
        sort(panel->tracks[t].intervals.begin(),panel->tracks[t].intervals.end(),CmpStartSmallToLarge);
    }
}
//maintain irouteList of track are sort
void ILPAssign::AssignIrouteToTrack_NTA(Track &track, Iroute &iroute, Interval interval)
{
    bool assign = false;
    for (int i=0;i<track.intervals.size();++i)
    {
        if (track.intervals[i].start > interval.start)//?
        {
            track.intervals.insert(track.intervals.begin()+i,interval);
            assign = true;
            break;
        }
    }

    if (!assign)
        track.intervals.push_back(interval);

    if (iroute.source.x == iroute.target.x)
        iroute.source.x = iroute.target.x = track.coordinate;
    else
        iroute.source.y = iroute.target.y = track.coordinate;

    if (iroute.subNetIndex != -1)
        taDB->netList[iroute.netIndex].subNetList[iroute.subNetIndex].assigned = true;
}


int ILPAssign::IroutesDistance(Iroute ir1,Iroute ir2)
{

    Iroute iroute1;
    Iroute iroute2;
    int distance = 0;

    int overlap;
//two horizontal iroutes
    if (ir1.source.y == ir1.target.y && ir2.source.y == ir2.target.y)
    {
        if (ir1.source.x < ir2.source.x)
        {
            iroute1 = ir1;
            iroute2 = ir2;
        }
        else
        {
            iroute1 = ir2;
            iroute2 = ir1;
        }

        overlap = min(iroute1.end , iroute2.end) - max(iroute1.start , iroute2.start);

//same y coordinate
        if (iroute1.source.y == iroute2.source.y)
        {
//connected
            if (overlap > -1)
                distance = 0;
            else
                distance = iroute2.start - iroute1.end;

        }
        else //different y coordinate
        {
//vertical distance
            distance = abs(iroute1.source.y-iroute2.source.y);

//horizontal distance when no overlap
            if (overlap < 0)
                distance += iroute2.start - iroute1.end;
        }

    }
    else if (ir1.source.x == ir1.target.x && ir2.source.x == ir2.target.x)
    {
        if (ir1.source.y < ir2.source.y)
        {
            iroute1 = ir1;
            iroute2 = ir2;
        }
        else
        {
            iroute1 = ir2;
            iroute2 = ir1;
        }

        overlap = min(iroute1.end , iroute2.end) - max(iroute1.start , iroute2.start);

//same x coordinate
        if (iroute1.source.x == iroute2.source.x)
        {
//connected
            if (overlap > -1)
                distance = 0;
            else
                distance = iroute2.start - iroute1.end;

        }
        else //different y coordinate
        {
//horizontal distance
            distance = abs(iroute1.source.x-iroute2.source.x);

//horizontal distance when no overlap
            if (overlap < 0)
                distance += iroute2.start - iroute1.end;
        }

    }
//one horizontal and one vertical iroutes
    else
    {
//let iroute1 horizontal, iroute2 vertical
        if (ir1.source.y == ir1.target.y)
        {
            iroute1 = ir1;
            iroute2 = ir2;
        }
        else
        {
            iroute1 = ir2;
            iroute2 = ir1;
        }

        bool overlapX = false;
        bool overlapY = false;

        if (iroute2.source.x >= iroute1.source.x && iroute2.source.x <= iroute1.target.x)
            overlapX = true;

        if (iroute1.source.y >= iroute2.source.y && iroute1.source.y <= iroute2.target.y)
            overlapY = true;

//intersection
        if (overlapX && overlapY)
            distance = 0;
        else if (overlapX) // T
            distance = min(abs(iroute1.source.y - iroute2.source.y),abs(iroute1.source.y - iroute2.target.y));
        else if (overlapY) // |-
            distance = min(abs(iroute2.source.x - iroute1.source.x),abs(iroute2.source.x - iroute1.target.x));
        else
        {
            int v1,v2,v3,v4;

            v1 = abs(iroute1.source.x - iroute2.source.x) + abs(iroute1.source.y - iroute2.source.y);
            v2 = abs(iroute1.source.x - iroute2.target.x) + abs(iroute1.source.y - iroute2.target.y);
            v3 = abs(iroute1.target.x - iroute2.source.x) + abs(iroute1.target.y - iroute2.source.y);
            v4 = abs(iroute1.target.x - iroute2.target.x) + abs(iroute1.target.y - iroute2.target.y);

            distance = min(min(min(v1,v2),v3),v4);
        }
    }
//    printf("minCost %d\n",distance);
    return distance;

    distance += abs(iroute1.source.layers - iroute2.source.layers) * VIA_COST;

    return distance;
}


int ILPAssign::IroutePinDistance(Iroute ir , Coordinate3D pin)
{
//pin.x *= 10;
//pin.y *= 10;

    int distance = 0;
    bool overlap = false;

//horizontal iroute
    if (ir.source.y == ir.target.y)
    {
        if (pin.x >= ir.source.x && pin.x <= ir.target.x)
            overlap = true;

//same y coordinate
        if (ir.source.y == pin.y)
        {
//connected
            if (overlap)
                distance = 0;
            else
                distance = min(abs(ir.source.x - pin.x),abs(ir.target.x-pin.x));

        }
        else //different y coordinate
        {
//vertical distance
            distance = abs(ir.source.y-pin.y);

//horizontal distance when no overlap
            if (!overlap)
                distance += min(abs(ir.source.x - pin.x),abs(ir.target.x-pin.x));
        }
    }
    else //vertical iroute
    {
        if (pin.y >= ir.source.y && pin.y <= ir.target.y)
            overlap = true;

//same y coordinate
        if (ir.source.x == pin.x)
        {
//connected
            if (overlap)
                distance = 0;
            else
                distance = min(abs(ir.source.y - pin.y),abs(ir.target.y-pin.y));

        }
        else //different y coordinate
        {
//vertical distance
            distance = abs(ir.source.x-pin.x);

//horizontal distance when no overlap
            if (!overlap)
                distance += min(abs(ir.source.y - pin.y),abs(ir.target.y-pin.y));
        }
    }

    return distance;

    distance += abs(ir.source.layers - pin.layers) * VIA_COST;

    return distance;
}

//Calculate wirelength cost
//taDB->irouteList[irouteIndex].netIndex,taDB->irouteList[irouteIndex].subNetIndex

GRBLinExpr ILPAssign::CalculateIrouteCost(Subpanel *subpanel,GRBVar **x)//wlcost
{
    GRBLinExpr wlcost;
    wlcost.clear();
    int irouteIndex,netIndex,subNetIndex;
    vector<double> tracksCost;
    GRBLinExpr minCost=0;
//minCost.clear();
    for(int ir=0;ir<subpanel->intervalList.size();++ir){
        irouteIndex = subpanel->intervalList[ir].irouteIndex;
        netIndex = taDB->irouteList[irouteIndex].netIndex;
        subNetIndex = taDB->irouteList[irouteIndex].subNetIndex;
        int i,j;
//int local_cost;
        int layer;
        int panelCoord;

//local net
        if (subNetIndex == -1){
            layer = taDB->netList[netIndex].trunk.source.layers;
            if (taDB->trackLayer[layer].isVertical){
                panelCoord = GetBinCoord(taDB->netList[netIndex].trunk.source.x,taDB->info.tileWidth);
                for (int k=0;k<taDB->trackLayer[layer].panels[panelCoord].tracks.size();++k)
                {

                    wlcost += x[ir][k] * abs(taDB->netList[netIndex].trunk.source.x -  taDB->trackLayer[layer].panels[panelCoord].tracks[k].coordinate);
//tracksCost.push_back(abs(taDB->netList[netIndex].trunk.source.x -  taDB->trackLayer[layer].panels[panelCoord].tracks[k].coordinate));

                }
            }
            else{
                panelCoord = GetBinCoord(taDB->netList[netIndex].trunk.source.y,taDB->info.tileHeight);
                for (int k=0;k<taDB->trackLayer[layer].panels[panelCoord].tracks.size();++k)
                    wlcost += x[ir][k] * abs(taDB->netList[netIndex].trunk.source.y -  taDB->trackLayer[layer].panels[panelCoord].tracks[k].coordinate);
//tracksCost.push_back(abs(taDB->netList[netIndex].trunk.source.y -  taDB->trackLayer[layer].panels[panelCoord].tracks[k].coordinate));
            }
//return wlcost;
            continue;
        }

//global net
        layer = taDB->netList[netIndex].subNetList[subNetIndex].source.layers;
        Iroute irouteOnTrack = taDB->netList[netIndex].subNetList[subNetIndex];
        Coordinate3D pin;
        int minCost ;
        if (taDB->trackLayer[layer].isVertical)
        {
            panelCoord = GetBinCoord(taDB->netList[netIndex].subNetList[subNetIndex].source.x,taDB->info.tileWidth);

//calculate every tracks cost
            for (i=0;i<taDB->trackLayer[layer].panels[panelCoord].tracks.size();++i)
            {
                minCost = 1e9;
                irouteOnTrack.source.x = irouteOnTrack.target.x = taDB->trackLayer[layer].panels[panelCoord].tracks[i].coordinate;

//iroutes connect cost
                for(j=0;j<taDB->netList[netIndex].subNetList.size();++j)
                {
                    if (!taDB->netList[netIndex].subNetList[j].assigned)
                        continue;

                    if (taDB->netList[netIndex].subNetList[subNetIndex].source.layers == taDB->netList[netIndex].subNetList[j].source.layers)
                        continue;
                    minCost = min(minCost,IroutesDistance(irouteOnTrack,taDB->netList[netIndex].subNetList[j]));
//minCost += x[ir][i] * IroutesDistance(irouteOnTrack,taDB->netList[netIndex].subNetList[j]);
                }

//pins connect cost
                for(j=0;j<taDB->netList[netIndex].pinList.size();++j){
                    pin = taDB->netList[netIndex].pinList[j];
                    pin.x*=10;
                    pin.y*=10;

                    minCost = min(minCost,IroutePinDistance(irouteOnTrack,pin));
//minCost += x[ir][i] * IroutePinDistance(irouteOnTrack,pin);
                }

                if(minCost == 1e9)
                    minCost = 0;

                wlcost += x[ir][i] * minCost;

            }

        }
        else
        {
            panelCoord = GetBinCoord(taDB->netList[netIndex].subNetList[subNetIndex].source.y,taDB->info.tileHeight);

//calculate every tracks cost
            for (i=0;i<taDB->trackLayer[layer].panels[panelCoord].tracks.size();++i){
                minCost = 1e9;
                irouteOnTrack.source.y = irouteOnTrack.target.y = taDB->trackLayer[layer].panels[panelCoord].tracks[i].coordinate;

//iroutes connect cost
                for(j=0;j<taDB->netList[netIndex].subNetList.size();++j){

                    if (!taDB->netList[netIndex].subNetList[j].assigned)
                        continue;

                    if (taDB->netList[netIndex].subNetList[subNetIndex].source.layers == taDB->netList[netIndex].subNetList[j].source.layers)
                        continue;
                    minCost = min(minCost,IroutesDistance(irouteOnTrack,taDB->netList[netIndex].subNetList[j]));
//minCost += x[ir][i] * IroutesDistance(irouteOnTrack,taDB->netList[netIndex].subNetList[j]);
                }

//pins connect cost
                for(j=0;j<taDB->netList[netIndex].pinList.size();++j)
                {
                    pin = taDB->netList[netIndex].pinList[j];
                    pin.x*=10;
                    pin.y*=10;

                    minCost = min(minCost,IroutePinDistance(irouteOnTrack,pin));
//minCost += x[ir][i] * IroutePinDistance(irouteOnTrack,pin);
                }

                if(minCost == 1e9)
                    minCost = 0;

                wlcost += x[ir][i] * minCost;

            }
        }


    }

    return wlcost;
}

vector<double> ILPAssign::CalculateIrouteCost_NTA(int netIndex,int subNetIndex)//wlcost
{
    int i,j;
    vector<double> tracksCost;
    int layer;
    int panelCoord;

//local net
    if (subNetIndex == -1)
    {
        layer = taDB->netList[netIndex].trunk.source.layers;

        if (taDB->trackLayer[layer].isVertical)
        {
            panelCoord = GetBinCoord(taDB->netList[netIndex].trunk.source.x,taDB->info.tileWidth);
            for (int k=0;k<taDB->trackLayer[layer].panels[panelCoord].tracks.size();++k)
                tracksCost.push_back(abs(taDB->netList[netIndex].trunk.source.x -  taDB->trackLayer[layer].panels[panelCoord].tracks[k].coordinate));
        }
        else
        {
            panelCoord = GetBinCoord(taDB->netList[netIndex].trunk.source.y,taDB->info.tileHeight);
            for (int k=0;k<taDB->trackLayer[layer].panels[panelCoord].tracks.size();++k)
                tracksCost.push_back(abs(taDB->netList[netIndex].trunk.source.y -  taDB->trackLayer[layer].panels[panelCoord].tracks[k].coordinate));
        }

        return tracksCost;
    }

//global net
    layer = taDB->netList[netIndex].subNetList[subNetIndex].source.layers;
    Iroute irouteOnTrack = taDB->netList[netIndex].subNetList[subNetIndex];
    Coordinate3D pin;
    int minCost;

    if (taDB->trackLayer[layer].isVertical)
    {
        panelCoord = GetBinCoord(taDB->netList[netIndex].subNetList[subNetIndex].source.x,taDB->info.tileWidth);

//calculate every tracks cost
        for (i=0;i<taDB->trackLayer[layer].panels[panelCoord].tracks.size();++i)
        {
            minCost = 1e9;
            irouteOnTrack.source.x = irouteOnTrack.target.x = taDB->trackLayer[layer].panels[panelCoord].tracks[i].coordinate;

//iroutes connect cost
            for(j=0;j<taDB->netList[netIndex].subNetList.size();++j)
            {
                if (!taDB->netList[netIndex].subNetList[j].assigned)
                    continue;

                if (taDB->netList[netIndex].subNetList[subNetIndex].source.layers == taDB->netList[netIndex].subNetList[j].source.layers)
                    continue;

/*if (taDB->trackLayer[taDB->netList[netIndex].subNetList[j].source.layers].isVertical)
{
//different panel
if (panelCoord != GetBinCoord(taDB->netList[netIndex].subNetList[j].source.x,taDB->info.tileWidth))
continue;
}
else
{
//different panel
if (panelCoord<GetBinCoord(taDB->netList[netIndex].subNetList[j].source.x,taDB->info.tileWidth) ||
panelCoord>GetBinCoord(taDB->netList[netIndex].subNetList[j].target.x,taDB->info.tileWidth))
continue;
}*/

                minCost = min(minCost,IroutesDistance(irouteOnTrack,taDB->netList[netIndex].subNetList[j]));
            }

//pins connect cost
            for(j=0;j<taDB->netList[netIndex].pinList.size();++j)
            {
                pin = taDB->netList[netIndex].pinList[j];
                pin.x*=10;
                pin.y*=10;

/*if (panelCoord != GetBinCoord(pin.x,taDB->info.tileWidth))
continue;*/
                minCost = min(minCost,IroutePinDistance(irouteOnTrack,pin));
            }

//this panel no any pins and iroutes of assigned
            if (minCost == 1e9)
                minCost = 0;
            tracksCost.push_back(minCost);
        }
    }
    else
    {
        panelCoord = GetBinCoord(taDB->netList[netIndex].subNetList[subNetIndex].source.y,taDB->info.tileHeight);

//calculate every tracks cost
        for (i=0;i<taDB->trackLayer[layer].panels[panelCoord].tracks.size();++i)
        {
            minCost = 1e9;
            irouteOnTrack.source.y = irouteOnTrack.target.y = taDB->trackLayer[layer].panels[panelCoord].tracks[i].coordinate;

//iroutes connect cost
            for(j=0;j<taDB->netList[netIndex].subNetList.size();++j)
            {

                if (!taDB->netList[netIndex].subNetList[j].assigned)
                    continue;

                if (taDB->netList[netIndex].subNetList[subNetIndex].source.layers == taDB->netList[netIndex].subNetList[j].source.layers)
                    continue;

/*if (!taDB->trackLayer[taDB->netList[netIndex].subNetList[j].source.layers].isVertical)
{
//different panel
if (panelCoord != GetBinCoord(taDB->netList[netIndex].subNetList[j].source.y,taDB->info.tileHeight))
continue;
}
else
{
//different panel
if (panelCoord<GetBinCoord(taDB->netList[netIndex].subNetList[j].source.y,taDB->info.tileHeight) ||
panelCoord>GetBinCoord(taDB->netList[netIndex].subNetList[j].target.y,taDB->info.tileHeight))
continue;
}*/

                minCost = min(minCost,IroutesDistance(irouteOnTrack,taDB->netList[netIndex].subNetList[j]));
            }

//pins connect cost
            for(j=0;j<taDB->netList[netIndex].pinList.size();++j)
            {
                pin = taDB->netList[netIndex].pinList[j];
                pin.x*=10;
                pin.y*=10;

/*if (panelCoord != GetBinCoord(pin.y,taDB->info.tileHeight))
continue;*/
                minCost = min(minCost,IroutePinDistance(irouteOnTrack,pin));
            }

//this panel no any pins and iroutes of assigned
            if (minCost == 1e9)
                minCost = 0;
            tracksCost.push_back(minCost);
        }
    }

    return tracksCost;
}


void ILPAssign::AssignRemIrouteToTrack(Panel *panel,Subpanel *subpanel){
    printf("subpanel->remov_intervalList.size():%d\n",subpanel->remov_intervalList.size());
    for (int in=0;in<subpanel->remov_intervalList.size();++in){//dAssign removeed iroutes
        int irouteIndex = subpanel->remov_intervalList[in].irouteIndex;

//wlcost
        vector<double> tracksCost = CalculateIrouteCost_NTA(taDB->irouteList[irouteIndex].netIndex,taDB->irouteList[irouteIndex].subNetIndex);
//printf("jyd\n");
//find min iroute overlap track of assignable tracks
        OverlapResult result;//overlap+blk
        OverlapResult minOverlapResult;
        double minCost = 999999999;
        int minOverlapIndex = 0;

        for (int k=0;k<panel->tracks.size();++k)
        {
            result = GetOverlapResult_NTA(panel->tracks[k],subpanel->remov_intervalList[in],false);
//printf("jyd1\n");
            double cost = GetCost_NTA(result) + 6000 * tracksCost[k];
//printf("jyd2\n");
            if (k==0)
                minCost = cost + 1;

            if (minCost > cost)
            {
                minOverlapResult = result;
                minCost = cost;
                minOverlapIndex = k;
            }
        }
//printf("jyd3\n");
//assign iroute to min cost track
        AssignIrouteToTrack_NTA(panel->tracks[minOverlapIndex],taDB->irouteList[irouteIndex],subpanel->remov_intervalList[in]);
//printf("jyd4\n");
//wlcost += tracksCost[minOverlapIndex];
//printf("minOverlapResult.overlapCount * minOverlapResult.overlapLengthￄ1�7?d,minOverlapResult.overlapBlkￄ1�7?d\n",minOverlapResult.overlapCount * minOverlapResult.overlapLength,minOverlapResult.overlapBlk);
//printf("\n");
        cost11 += ((minOverlapResult.overlapCount * minOverlapResult.overlapLength)/10);//olpcost
        cost22 += (minOverlapResult.overlapBlk/10);//blkcost
//printf("tackcost:%lf\n",tracksCost[minOverlapIndex]);
//omp_set_lock(&mylock);
//if (assignCount%10000 == 0)
//printf("iroutes %d\n",assignCount);

//assignCount++;
//omp_unset_lock(&mylock);
    }

}

void ILPAssign::IlpMethod()
{

    int cutpanel=0;
//update
    for (int i=0;i<taDB->irouteList.size();++i)
    {
        if (taDB->irouteList[i].subNetIndex == -1)
            taDB->netList[taDB->irouteList[i].netIndex].trunk = taDB->irouteList[i];
        else
            taDB->netList[taDB->irouteList[i].netIndex].subNetList[taDB->irouteList[i].subNetIndex] = taDB->irouteList[i];
    }

    int layer;
    int panelCoord;
    Interval tempInterval;

    for (int i=0;i<taDB->irouteList.size();++i)
    {
        layer = taDB->irouteList[i].source.layers;
//get the coord of the panel
        if (taDB->trackLayer[layer].isVertical)//vertical
        {
            panelCoord = GetBinCoord(taDB->irouteList[i].source.x,taDB->info.tileWidth);
        }
        else//horizontal
        {
            panelCoord = GetBinCoord(taDB->irouteList[i].source.y,taDB->info.tileHeight);
        }
        tempInterval.irouteIndex = i;
        tempInterval.iroutesPinsCount = taDB->netList[taDB->irouteList[i].netIndex].subNetList.size()+taDB->netList[taDB->irouteList[i].netIndex].pinList.size();//?
        tempInterval.start = taDB->irouteList[i].start;
        tempInterval.end = taDB->irouteList[i].end;
// printf("start:%d;end:%d\n",tempInterval.start,tempInterval.end);
        taDB->trackLayer[layer].panels[panelCoord].intervalList.push_back(tempInterval);
    }

    printf("number of iroute %d\n",taDB->irouteList.size());




//assign
    omp_lock_t mylock;
    omp_init_lock(&mylock);
//omp_set_num_threads(40);

    int *mapValue,count;

    for(int i=2;i<taDB->trackLayer.size();++i)//Layer
    {
        count = 0;
        mapValue = new int[taDB->trackLayer[i].panels.size()];
        for (map<int,Panel>::iterator it = taDB->trackLayer[i].panels.begin();it!=taDB->trackLayer[i].panels.end();++it)
        {
            mapValue[count] = it->first;//panelCoord
            ++count;//panel
        }


        #pragma omp parallel for schedule(dynamic,1)
        for(int l=0;l<taDB->trackLayer[i].panels.size();++l)//panel,
        {
            Panel *panel = &taDB->trackLayer[i].panels[mapValue[l]];
            Interval interval;


//dividing the tracks into pieces

            int *coord,cnt=0;

            coord = new int[panel->intervalList.size()*2];

//dividing
            for(int in=0;in<panel->intervalList.size();in++){
                coord[cnt++] = panel->intervalList[in].start;
                coord[cnt++] = panel->intervalList[in].end;
            }

            sort(coord,coord+panel->intervalList.size()*2);
            int numofsegments = unique(coord,coord+panel->intervalList.size()*2)-coord;
            printf("num of segments:%d\n",numofsegments);
            cout<<" layer:"<<i<<" panel:"<<l<<endl;
            cout<<"iroutes of panel:"<<panel->intervalList.size()<<endl;

            short int **c;
            c = new short int*[panel->intervalList.size()];
            for(int in = 0;in<panel->intervalList.size();++in){
                c[in] = new short int[numofsegments-1];
                memset(c[in],0,(numofsegments-1) * sizeof(short int));
            }

            for(int in = 0;in<panel->intervalList.size();++in){
                for(int k = 0;k<numofsegments-1;++k){
                    if(panel->intervalList[in].start == coord[k]){
                        while(coord[k] != panel->intervalList[in].end)
                            c[in][k++] = 1;
                        break;
                    }
                }
            }

            int tag,startofsegment=0,subpanelconut = 0;
            Subpanel sp;
            sp.start = coord[0];

//stage one of Reduction
            for(int k = 0;k < numofsegments - 1;k++){//sections

                tag = 0;
                for(int in = 0;in < panel->intervalList.size();in++){//iroutes
                    if(c[in][k]){
                        tag = 1;
                        break;
                    }
                }

                if(tag)
                    continue;

                if(!tag){
                    sp.end = coord[k];
                    sp.numofsegments = k - startofsegment;

                    sp.source = startofsegment;

                    startofsegment = k + 1;
                    panel->subpanels.insert(map<int,Subpanel>::value_type(subpanelconut++,sp));
                    sp.start = coord[k+1];
                }

            }
            sp.end = coord[numofsegments - 1];
            sp.numofsegments = numofsegments - 1 - startofsegment;

            sp.source = startofsegment;

            panel->subpanels.insert(map<int,Subpanel>::value_type(subpanelconut,sp));




//add iroutes according to the subpanel
            for(int s = 0;s<panel->subpanels.size();s++){
                for(int in=0;in<panel->intervalList.size();in++){
                    if(panel->intervalList[in].start >= panel->subpanels[s].start && panel->intervalList[in].end <= panel->subpanels[s].end)
                        panel->subpanels[s].intervalList.push_back(panel->intervalList[in]);//
                }
            }

//丢�阶段更新c
            for(int sub = 0;sub<panel->subpanels.size();sub++){

//short int **c;
//c[i][k]:whether iroute i is on the k-th segment
                panel->subpanels[sub].c = new short int*[panel->subpanels[sub].intervalList.size()];
                for(int in = 0;in<panel->subpanels[sub].intervalList.size();++in){
                    panel->subpanels[sub].c[in] = new short int[panel->subpanels[sub].numofsegments];
                    memset(panel->subpanels[sub].c[in],0,(panel->subpanels[sub].numofsegments) * sizeof(short int));
                }

                for(int in = 0;in<panel->subpanels[sub].intervalList.size();++in){
                    for(int k = 0;k<panel->subpanels[sub].numofsegments;++k){
                        if(panel->subpanels[sub].intervalList[in].start == coord[k + panel->subpanels[sub].source]){
                            while(coord[k + panel->subpanels[sub].source] != panel->subpanels[sub].intervalList[in].end)
                                panel->subpanels[sub].c[in][k++] = 1;
                            break;
                        }
                    }
                }
            }

//stage two of Reduction
            for(int sub = 0;sub<panel->subpanels.size();sub++){


//超过可容忍最大subpanel规模则进行切ￄ1�7?
                if(panel->subpanels[sub].intervalList.size() > upperbound_iroutenum){


                    int pos,min_count=99999;//切断的位置，朢�小切断数ￄ1�7?每个coord可切断数量计ￄ1�7?

//遍历1/3-2/3处��择丢�个涉及导线最少的coord切断
                    for(int k = (panel->subpanels[sub].numofsegments/3);k<=((panel->subpanels[sub].numofsegments*2)/3);++k){
                        if(k-1<0) continue;
                        int count=0;
//求各coord的切割数
                        for(int in  = 0;in<panel->subpanels[sub].intervalList.size();in++){

                            if(panel->subpanels[sub].c[in][k-1]==1 && panel->subpanels[sub].c[in][k] ==1){
                                count++;
                            }
                        }
//记录具有朢�小切割数的坐ￄ1�7?
                        if(count < min_count){
                            min_count = count;
                            pos = k;
                        }
                    }
//大于可容忍切割导线数则不进行分割 icut
                    if(min_count > upperbound_iroutecut){

                        continue;
                    }

                    else{
                        omp_set_lock(&mylock);
                        cutpanel++;
                        omp_unset_lock(&mylock);
                        Subpanel sp1,sp2;

//将这个subpanel按照记录的pos分割
                        vector<Interval>::iterator iter1 = panel->subpanels[sub].intervalList.begin();
                        for(int in  = panel->subpanels[sub].intervalList.size()- 1;in >= 0;in--){
                            Interval temp_intv = panel->subpanels[sub].intervalList[in];

//该导线若被切断则取出，加入另丢�个容器中
                            if(temp_intv.start < coord[pos+panel->subpanels[sub].source] && temp_intv.end > coord[pos+panel->subpanels[sub].source]){

                                sp2.remov_intervalList.push_back(panel->subpanels[sub].intervalList[in]);
                                panel->subpanels[sub].intervalList.erase(iter1 + in);
                            }

                        }



                        sp1.start = panel->subpanels[sub].start;
                        sp1.end = coord[pos+panel->subpanels[sub].source];
                        sp2.start = sp1.end;
                        sp2.end = panel->subpanels[sub].end;
                        sp1.source=panel->subpanels[sub].source;
                        sp2.source=panel->subpanels[sub].source+pos;
                        sp1.numofsegments=pos;
                        sp2.numofsegments=panel->subpanels[sub].numofsegments-pos;



//分配导线到sp1 & sp2
                        for(int in  = 0;in < panel->subpanels[sub].intervalList.size();in++){
                            if(panel->subpanels[sub].intervalList[in].start >= sp1.start && panel->subpanels[sub].intervalList[in].end <= sp1.end){
                                sp1.intervalList.push_back(panel->subpanels[sub].intervalList[in]);
                            }
                            else
                                sp2.intervalList.push_back(panel->subpanels[sub].intervalList[in]);

                        }


//更新C
//sp1.c
                        sp1.c = new short int*[sp1.intervalList.size()];
                        for(int in = 0;in<sp1.intervalList.size();++in){
                            sp1.c[in] = new short int[sp1.numofsegments];
                            memset(sp1.c[in],0,(sp1.numofsegments) * sizeof(short int));
                        }

                        for(int in = 0;in<sp1.intervalList.size();++in){
                            for(int k = 0;k<sp1.numofsegments;++k){
                                if(sp1.intervalList[in].start == coord[k + sp1.source]){
                                    while(coord[k + sp1.source] != sp1.intervalList[in].end)
                                        sp1.c[in][k++] = 1;
                                    break;
                                }
                            }
                        }



//sp2.c
                        sp2.c = new short int*[sp2.intervalList.size()];
                        for(int in = 0;in<sp2.intervalList.size();++in){
                            sp2.c[in] = new short int[sp2.numofsegments];
                            memset(sp2.c[in],0,(sp2.numofsegments) * sizeof(short int));
                        }

                        for(int in = 0;in<sp2.intervalList.size();++in){
                            for(int k = 0;k<sp2.numofsegments;++k){
                                if(sp2.intervalList[in].start == coord[k + sp2.source]){
                                    while(coord[k + sp2.source]!= sp2.intervalList[in].end)
                                        sp2.c[in][k++] = 1;
                                    break;
                                }
                            }
                        }


//移除并加入分割后的subpanel
                        map<int,Subpanel>::iterator iter = panel->subpanels.find(sub);
                        panel->subpanels.erase(iter);
                        panel->subpanels.insert(map<int,Subpanel>::value_type(sub,sp1));
                        panel->subpanels.insert(map<int,Subpanel>::value_type(panel->subpanels.size(),sp2));


                    }

                }

            }


            printf("num of supanels:%d\n",panel->subpanels.size());






            //TODO ILP
            #pragma omp parallel for schedule(dynamic,1)
            for(int sub = 0;sub<panel->subpanels.size();sub++)//panel->subpanels.size()
            {
                auto start_time = std::chrono::high_resolution_clock::now();

                Subpanel *subpanel = &panel->subpanels[sub];


                printf("subpanels.intervalist.size():%d\n",subpanel->intervalList.size());
                cout<<"track_size"<<panel->tracks.size()<<endl;
                try{
// Create an environment
                    GRBEnv env = GRBEnv(true);
//					env.set("LogFile", "ilp.log");
                    env.start();

// Create an empty model
                    GRBModel model = GRBModel(env);
                    model.getEnv().set(GRB_IntParam_OutputFlag, 0);
//create variables
                    GRBVar **x=0;//First declare an array pointing to an array
                    x = new GRBVar*[subpanel->intervalList.size()];
                    for (int j=0;j<subpanel->intervalList.size();++j)//iroutes
                    {
//create decision variables
                        x[j] = model.addVars(panel->tracks.size(),GRB_BINARY);
                    }

// Set objective: minimize blkcost+overlapcost

//calculate overlap cost
                    OverlapResult overlapresult,blkresult,result;

                    GRBVar **onum;
                    onum = new GRBVar*[panel->tracks.size()];
                    for(int t = 0;t<panel->tracks.size();++t)
                        onum[t] = model.addVars(subpanel->numofsegments,GRB_INTEGER);


                    result.overlapCount = 0;
                    result.overlapLength = 0;
                    result.overlapBlk = 0;

                    GRBLinExpr cost1;//overlap cost
                    cost1.clear();
//calculating overlap length
                    for(int t = 0; t<panel->tracks.size();++t){
                        for(int k = 0;k<subpanel->numofsegments;++k){
                            cost1 += onum[t][k] * (coord[k+1+subpanel->source]-coord[k+subpanel->source]);
                        }
                    }


                    GRBLinExpr cost2;//blockage cost
                    cost2.clear();
//calculating blkcost
                    for (int in = 0;in < subpanel->intervalList.size();in++) {
                        for (int t = 0;t < panel->tracks.size();t++) {
                            cost2 += x[in][t] * GetBlkResult(result, panel->tracks[t], subpanel->intervalList[in], false).overlapBlk;
                        }
                    }


//wlcost
                    GRBLinExpr wlcost;
                    wlcost.clear();
                    wlcost = CalculateIrouteCost(subpanel,x);


//GRBLinExpr cost = GetCost(result);
//cost1:olp;cost2,blk;wlcost
                    model.setObjective(ol_cost_weight*cost1+blk_cost_weight*cost2+wl_cost_weight*wlcost, GRB_MINIMIZE);

//Add constraint

                    GRBLinExpr c0, c1;
                    c1.clear();

//constraint 1: every iroute a constraint;every constraint num of tracks size
                    for(int in=0;in<subpanel->intervalList.size();in++)
                    {
                        for(int t=0;t<panel->tracks.size();t++)
                        {
                            c1 = c1 + x[in][t];//constraint 0
                        }
// Add constraint:丢�个iroute不可分给两个track
                        model.addConstr(c1 == 1);
                        c1.clear();

                    }

//constraint 2/**/
//c0.clear();
                    for(int t = 0;t<panel->tracks.size();++t){
                        for(int k = 0;k<subpanel->numofsegments;++k){
//c0 = onum[t][k];
// Add constraint:keep the value of onum is greater than 0
                            model.addConstr(onum[t][k] >= 0);//
//c0.clear();
                        }

                    }


//constraint 4
                    GRBLinExpr c3;
                    c3.clear();
                    for(int t = 0;t<panel->tracks.size();++t){
                        for(int k = 0;k<subpanel->numofsegments;++k){
                            for(int in = 0;in<subpanel->intervalList.size();++in)
                                c3  += subpanel->c[in][k] * x[in][t];
                            c3 -= 1;
                            model.addConstr(onum[t][k]>=c3);
                            c3.clear();
                        }
                    }

// Optimize model

//model.write("tmp.lp");
//model.computeIIS();

                    model.optimize();


//assign iroute to min cost track
                    AssignIrouteToTrack(panel,subpanel,x);

//对移除的iroutes进行再分ￄ1�7?
                    if(subpanel->remov_intervalList.size() > 0)
                    {
                        printf("subpanel->remov_intervalList.size():%d\n",subpanel->remov_intervalList.size());
                        AssignRemIrouteToTrack(panel,subpanel);
                    }


                    omp_set_lock(&mylock);
                    total_blkcost+=(cost2.getValue()/10);
                    total_olpcost+=(cost1.getValue()/10);
                    total_wlcost+=(wlcost.getValue()/10);
                    cost11 += (cost1.getValue()/10);
                    cost22 += (cost2.getValue()/10);
                    omp_unset_lock(&mylock);

                    for(int t = 0;t<panel->tracks.size();++t)
                        delete []onum[t];
                    delete onum;
//for(int t = 0;t<panel->tracks.size();++t)
//delete []onumm[t];
//delete onumm;
                    for(int j=0;j<subpanel->intervalList.size();++j)
                        delete []x[j];
                    delete x;
                    for(int in = 0;in<subpanel->intervalList.size();++in)
                        delete []subpanel->c[in];
                    delete subpanel->c;
                }catch(GRBException e){
                    cout<<"Error code ="<<e.getErrorCode()<<endl;
                    cout<<e.getMessage()<<endl;
                }
                catch(const std::exception& e)
                {   cout<<"Exception during optimization"<<endl;
                    std::cerr << "Caught exception: " << e.what() << std::endl;
                }

                auto end_time = std::chrono::high_resolution_clock::now();

                // 计算时间差异
                auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

                // 输出执行时间
                std::cout << "Runtime: " << duration.count() << " ms" << std::endl;

            }
            for(int in = 0;in<panel->intervalList.size();++in)
                delete []c[in];
            delete c;
            delete []coord;

        }
    }


//Update netList
    for (int i=0;i<taDB->irouteList.size();++i)
    {
//local net
        if (taDB->irouteList[i].subNetIndex == -1)
            taDB->netList[taDB->irouteList[i].netIndex].trunk = taDB->irouteList[i];
        else//global net
            taDB->netList[taDB->irouteList[i].netIndex].subNetList[taDB->irouteList[i].subNetIndex] = taDB->irouteList[i];
    }

    printf("The num of cut panel is %d\n",cutpanel);
}

void ILPAssign::DPMethod(){

    //addVectors_test();

    int cutpanel=0;
//update
    for (int i=0;i<taDB->irouteList.size();++i)
    {
        if (taDB->irouteList[i].subNetIndex == -1)
            taDB->netList[taDB->irouteList[i].netIndex].trunk = taDB->irouteList[i];
        else
            taDB->netList[taDB->irouteList[i].netIndex].subNetList[taDB->irouteList[i].subNetIndex] = taDB->irouteList[i];
    }

    int layer;
    int panelCoord;
    Interval tempInterval;

    for (int i=0;i<taDB->irouteList.size();++i)
    {
        layer = taDB->irouteList[i].source.layers;
//get the coord of the panel
        if (taDB->trackLayer[layer].isVertical)//vertical
        {
            panelCoord = GetBinCoord(taDB->irouteList[i].source.x,taDB->info.tileWidth);
        }
        else//horizontal
        {
            panelCoord = GetBinCoord(taDB->irouteList[i].source.y,taDB->info.tileHeight);
        }
        tempInterval.irouteIndex = i;
        tempInterval.iroutesPinsCount = taDB->netList[taDB->irouteList[i].netIndex].subNetList.size()+taDB->netList[taDB->irouteList[i].netIndex].pinList.size();//?
        tempInterval.start = taDB->irouteList[i].start;
        tempInterval.end = taDB->irouteList[i].end;
// printf("start:%d;end:%d\n",tempInterval.start,tempInterval.end);
        taDB->trackLayer[layer].panels[panelCoord].intervalList.push_back(tempInterval);
    }

    printf("number of iroute %d\n",taDB->irouteList.size());




//assign
    omp_lock_t mylock;
    omp_init_lock(&mylock);
//omp_set_num_threads(40);

    int *mapValue,count;

    for(int i=2;i<taDB->trackLayer.size();++i)//Layer
    {
        count = 0;
        mapValue = new int[taDB->trackLayer[i].panels.size()];
        for (map<int,Panel>::iterator it = taDB->trackLayer[i].panels.begin();it!=taDB->trackLayer[i].panels.end();++it)
        {
            mapValue[count] = it->first;//panelCoord
            ++count;//panel
        }


        #pragma omp parallel for schedule(dynamic,1)
        for(int l=0;l<taDB->trackLayer[i].panels.size();++l)//panel,
        {
            Panel *panel = &taDB->trackLayer[i].panels[mapValue[l]];
            Interval interval;

//dividing the tracks into pieces

            int *coord,cnt=0;

            coord = new int[panel->intervalList.size()*2];

//dividing
            for(int in=0;in<panel->intervalList.size();in++){
                coord[cnt++] = panel->intervalList[in].start;
                coord[cnt++] = panel->intervalList[in].end;
            }

            sort(coord,coord+panel->intervalList.size()*2);//sort from small to large

//            sort(panel->intervalList.begin(),panel->intervalList.end(),[&](Interval a,Interval b)   {
//                return a.end-a.start>b.end-b.start;
//            });

//deduplication
            int numofsegments = unique(coord,coord+panel->intervalList.size()*2)-coord;
            printf("num of segments:%d\n",numofsegments-1);
            cout<<" layer:"<<i<<" panel:"<<l<<endl;
            cout<<"iroutes of panel:"<<panel->intervalList.size()<<endl;

            short int **c;
//c[i][k]:whether iroute i is on the k-th segment
            c = new short int*[panel->intervalList.size()];
            for(int in = 0;in<panel->intervalList.size();++in){
                c[in] = new short int[numofsegments-1];
                memset(c[in],0,(numofsegments-1) * sizeof(short int));
            }

            for(int in = 0;in<panel->intervalList.size();++in){
                for(int k = 0;k<numofsegments-1;++k){
                    if(panel->intervalList[in].start == coord[k]){
                        while(coord[k] != panel->intervalList[in].end)
                            c[in][k++] = 1;
                        break;
                    }
                }
            }

            int tag,startofsegment=0,subpanelconut = 0;
            Subpanel sp;
            sp.start = coord[0];

//stage one of Reduction
            for(int k = 0;k < numofsegments - 1;k++){//sections

                tag = 0;
                for(int in = 0;in < panel->intervalList.size();in++){//iroutes
                    if(c[in][k]){
                        tag = 1;
                        break;
                    }
                }

                if(tag)
                    continue;

                if(!tag){
                    sp.end = coord[k];
                    sp.numofsegments = k - startofsegment;

                    sp.source = startofsegment;

                    startofsegment = k + 1;
                    panel->subpanels.insert(map<int,Subpanel>::value_type(subpanelconut++,sp));
                    sp.start = coord[k+1];
                }

            }
            sp.end = coord[numofsegments - 1];
            sp.numofsegments = numofsegments - 1 - startofsegment;

            sp.source = startofsegment;

            panel->subpanels.insert(map<int,Subpanel>::value_type(subpanelconut,sp));




//add iroutes according to the subpanel
            for(int s = 0;s<panel->subpanels.size();s++){
                for(int in=0;in<panel->intervalList.size();in++){
                    if(panel->intervalList[in].start >= panel->subpanels[s].start && panel->intervalList[in].end <= panel->subpanels[s].end)
                        panel->subpanels[s].intervalList.push_back(panel->intervalList[in]);//
                }
            }

//丢�阶段更新c
            for(int sub = 0;sub<panel->subpanels.size();sub++){

//short int **c;
//c[i][k]:whether iroute i is on the k-th segment
                panel->subpanels[sub].c = new short int*[panel->subpanels[sub].intervalList.size()];
                for(int in = 0;in<panel->subpanels[sub].intervalList.size();++in){
                    panel->subpanels[sub].c[in] = new short int[panel->subpanels[sub].numofsegments];
                    memset(panel->subpanels[sub].c[in],0,(panel->subpanels[sub].numofsegments) * sizeof(short int));
                }

                for(int in = 0;in<panel->subpanels[sub].intervalList.size();++in){
                    for(int k = 0;k<panel->subpanels[sub].numofsegments;++k){
                        if(panel->subpanels[sub].intervalList[in].start == coord[k + panel->subpanels[sub].source]){
                            while(coord[k + panel->subpanels[sub].source] != panel->subpanels[sub].intervalList[in].end)
                                panel->subpanels[sub].c[in][k++] = 1;
                            break;
                        }
                    }
                }
            }

//stage two of Reduction
//            for(int sub = 0;sub<panel->subpanels.size();sub++)
                for(int sub = 0;sub<0;sub++)
            {


//超过可容忍最大subpanel规模则进行切ￄ1�7?
                if(panel->subpanels[sub].intervalList.size() > upperbound_iroutenum){


                    int pos,min_count=99999;//切断的位置，朢�小切断数ￄ1�7?每个coord可切断数量计ￄ1�7?

//遍历1/3-2/3处��择丢�个涉及导线最少的coord切断
                    for(int k = (panel->subpanels[sub].numofsegments/3);k<=((panel->subpanels[sub].numofsegments*2)/3);++k){
                        if(k-1<0) continue;
                        int count=0;
//求各coord的切割数
                        for(int in  = 0;in<panel->subpanels[sub].intervalList.size();in++){

                            if(panel->subpanels[sub].c[in][k-1]==1 && panel->subpanels[sub].c[in][k] ==1){
                                count++;
                            }
                        }
//记录具有朢�小切割数的坐ￄ1�7?
                        if(count < min_count){
                            min_count = count;
                            pos = k;
                        }
                    }
//大于可容忍切割导线数则不进行分割 icut
                    if(min_count > upperbound_iroutecut){

                        continue;
                    }

                    else{
                        omp_set_lock(&mylock);
                        cutpanel++;
                        omp_unset_lock(&mylock);
                        Subpanel sp1,sp2;

//将这个subpanel按照记录的pos分割
                        vector<Interval>::iterator iter1 = panel->subpanels[sub].intervalList.begin();
                        for(int in  = panel->subpanels[sub].intervalList.size()- 1;in >= 0;in--){
                            Interval temp_intv = panel->subpanels[sub].intervalList[in];

//该导线若被切断则取出，加入另丢�个容器中
                            if(temp_intv.start < coord[pos+panel->subpanels[sub].source] && temp_intv.end > coord[pos+panel->subpanels[sub].source]){

                                sp2.remov_intervalList.push_back(panel->subpanels[sub].intervalList[in]);
                                panel->subpanels[sub].intervalList.erase(iter1 + in);
                            }

                        }



                        sp1.start = panel->subpanels[sub].start;
                        sp1.end = coord[pos+panel->subpanels[sub].source];
                        sp2.start = sp1.end;
                        sp2.end = panel->subpanels[sub].end;
                        sp1.source=panel->subpanels[sub].source;
                        sp2.source=panel->subpanels[sub].source+pos;
                        sp1.numofsegments=pos;
                        sp2.numofsegments=panel->subpanels[sub].numofsegments-pos;



//分配导线到sp1 & sp2
                        for(int in  = 0;in < panel->subpanels[sub].intervalList.size();in++){
                            if(panel->subpanels[sub].intervalList[in].start >= sp1.start && panel->subpanels[sub].intervalList[in].end <= sp1.end){
                                sp1.intervalList.push_back(panel->subpanels[sub].intervalList[in]);
                            }
                            else
                                sp2.intervalList.push_back(panel->subpanels[sub].intervalList[in]);

                        }


//更新C
//sp1.c
                        sp1.c = new short int*[sp1.intervalList.size()];
                        for(int in = 0;in<sp1.intervalList.size();++in){
                            sp1.c[in] = new short int[sp1.numofsegments];
                            memset(sp1.c[in],0,(sp1.numofsegments) * sizeof(short int));
                        }

                        for(int in = 0;in<sp1.intervalList.size();++in){
                            for(int k = 0;k<sp1.numofsegments;++k){
                                if(sp1.intervalList[in].start == coord[k + sp1.source]){
                                    while(coord[k + sp1.source] != sp1.intervalList[in].end)
                                        sp1.c[in][k++] = 1;
                                    break;
                                }
                            }
                        }



//sp2.c
                        sp2.c = new short int*[sp2.intervalList.size()];
                        for(int in = 0;in<sp2.intervalList.size();++in){
                            sp2.c[in] = new short int[sp2.numofsegments];
                            memset(sp2.c[in],0,(sp2.numofsegments) * sizeof(short int));
                        }

                        for(int in = 0;in<sp2.intervalList.size();++in){
                            for(int k = 0;k<sp2.numofsegments;++k){
                                if(sp2.intervalList[in].start == coord[k + sp2.source]){
                                    while(coord[k + sp2.source]!= sp2.intervalList[in].end)
                                        sp2.c[in][k++] = 1;
                                    break;
                                }
                            }
                        }


//移除并加入分割后的subpanel
                        map<int,Subpanel>::iterator iter = panel->subpanels.find(sub);
                        panel->subpanels.erase(iter);
                        panel->subpanels.insert(map<int,Subpanel>::value_type(sub,sp1));
                        panel->subpanels.insert(map<int,Subpanel>::value_type(panel->subpanels.size(),sp2));


                    }

                }

            }


            printf("num of supanels:%d\n",panel->subpanels.size());



            //TODO DP
            #pragma omp parallel for schedule(dynamic,1)
            for(int sub = 0;sub<panel->subpanels.size();sub++)//panel->subpanels.size()
            {
                auto start_time = std::chrono::high_resolution_clock::now();

                Subpanel *subpanel = &panel->subpanels[sub];

                if (subpanel->intervalList.size() == 0) continue;


                vector<pair<int,int>> c_startToEnd(subpanel->intervalList.size());
                for(int in = 0;in<subpanel->intervalList.size();in++){
                    int start = -1;
                    int end = -1;
                    for(int s=0;s<subpanel->numofsegments;s++){
                        if(subpanel->c[in][s]==1&&start == -1){
                            start = s;
                        }
                        else if(subpanel->c[in][s]==0&&start != -1){
                            end = s-1;
                            break;
                        }
                    }
                    if(end==-1&&start != -1){
                        end = subpanel->numofsegments-1;
                    }
                    c_startToEnd[in] = make_pair(start,end);
                }


// calculate cost2*1000

//                vector<vector<bool>> x(subpanel->intervalList.size(), vector<bool>(panel->tracks.size(), false));
                OverlapResult overlapresult, blkresult, result;
                result.overlapCount = 0;
                result.overlapLength = 0;
                result.overlapBlk = 0;
                vector<vector<double>> blkcost(subpanel->intervalList.size(), vector<double>(panel->tracks.size(), 0));

                for (int in = 0; in < subpanel->intervalList.size(); in++) {
                    for (int t = 0; t < panel->tracks.size(); t++) {
                        blkcost[in][t] = GetBlkResult(result, panel->tracks[t], subpanel->intervalList[in],
                                                      false).overlapBlk;
//                         cout<<std::setw(7)<<blkcost[in][t];
                    }
//                    cout<<endl;
                }

// calculate cost1

// calculate wlcost
                vector<vector<double>> wlcost(subpanel->intervalList.size(), vector<double>(panel->tracks.size(), 0));

                int irouteIndex, netIndex, subNetIndex;
                vector<double> tracksCost;
                int minCost = 0;
                for (int ir = 0; ir < subpanel->intervalList.size(); ++ir) {
                    irouteIndex = subpanel->intervalList[ir].irouteIndex;
                    netIndex = taDB->irouteList[irouteIndex].netIndex;
                    subNetIndex = taDB->irouteList[irouteIndex].subNetIndex;
//int local_cost;
                    int layer;
                    int panelCoord;

//local net
                    if (subNetIndex == -1) {
                        layer = taDB->netList[netIndex].trunk.source.layers;
                        if (taDB->trackLayer[layer].isVertical) {
                            panelCoord = GetBinCoord(taDB->netList[netIndex].trunk.source.x, taDB->info.tileWidth);
                            for (int k = 0; k < taDB->trackLayer[layer].panels[panelCoord].tracks.size(); ++k) {
                                wlcost[ir][k] += abs(taDB->netList[netIndex].trunk.source.x -
                                                     taDB->trackLayer[layer].panels[panelCoord].tracks[k].coordinate);
//tracksCost.push_back(abs(taDB->netList[netIndex].trunk.source.x -  taDB->trackLayer[layer].panels[panelCoord].tracks[k].coordinate));
                            }
                        } else {
                            panelCoord = GetBinCoord(taDB->netList[netIndex].trunk.source.y, taDB->info.tileHeight);
                            for (int k = 0; k < taDB->trackLayer[layer].panels[panelCoord].tracks.size(); ++k)
                                wlcost[ir][k] += abs(taDB->netList[netIndex].trunk.source.y -
                                                     taDB->trackLayer[layer].panels[panelCoord].tracks[k].coordinate);
//tracksCost.push_back(abs(taDB->netList[netIndex].trunk.source.y -  taDB->trackLayer[layer].panels[panelCoord].tracks[k].coordinate));
                        }
//return wlcost;
                        continue;
                    }
//global net
                    layer = taDB->netList[netIndex].subNetList[subNetIndex].source.layers;
                    Iroute irouteOnTrack = taDB->netList[netIndex].subNetList[subNetIndex];
                    Coordinate3D pin;

                    if (taDB->trackLayer[layer].isVertical) {
                        panelCoord = GetBinCoord(taDB->netList[netIndex].subNetList[subNetIndex].source.x,
                                                 taDB->info.tileWidth);
                        int minCost = 1e9;
//calculate every tracks cost
                        for (int t = 0; t < taDB->trackLayer[layer].panels[panelCoord].tracks.size(); ++t) {
//minCost = 1e9;
                            irouteOnTrack.source.x = irouteOnTrack.target.x = taDB->trackLayer[layer].panels[panelCoord].tracks[t].coordinate;

//iroutes connect cost
                            for (int n = 0; n < taDB->netList[netIndex].subNetList.size(); ++n) {
                                if (!taDB->netList[netIndex].subNetList[n].assigned)
                                    continue;

                                if (taDB->netList[netIndex].subNetList[subNetIndex].source.layers ==
                                    taDB->netList[netIndex].subNetList[n].source.layers)
                                    continue;
                                minCost = min(minCost,
                                              IroutesDistance(irouteOnTrack, taDB->netList[netIndex].subNetList[n]));
//minCost += x[ir][t] * IroutesDistance(irouteOnTrack,taDB->netList[netIndex].subNetList[n]);
                            }

//pins connect cost
                            for (int n = 0; n < taDB->netList[netIndex].pinList.size(); ++n) {
                                pin = taDB->netList[netIndex].pinList[n];
                                pin.x *= 10;
                                pin.y *= 10;

                                minCost = min(minCost, IroutePinDistance(irouteOnTrack, pin));
//minCost += x[ir][t] * IroutePinDistance(irouteOnTrack,pin);
                            }

                            if (minCost == 1e9)
                                minCost = 0;

                            wlcost[ir][t] += minCost;
                        }

                    } else {
                        panelCoord = GetBinCoord(taDB->netList[netIndex].subNetList[subNetIndex].source.y,
                                                 taDB->info.tileHeight);
                        int minCost = 1e9;
//calculate every tracks cost
                        for (int t = 0; t < taDB->trackLayer[layer].panels[panelCoord].tracks.size(); ++t) {

                            irouteOnTrack.source.y = irouteOnTrack.target.y = taDB->trackLayer[layer].panels[panelCoord].tracks[t].coordinate;

//iroutes connect cost
                            for (int n = 0; n < taDB->netList[netIndex].subNetList.size(); ++n) {

                                if (!taDB->netList[netIndex].subNetList[n].assigned)
                                    continue;

                                if (taDB->netList[netIndex].subNetList[subNetIndex].source.layers ==
                                    taDB->netList[netIndex].subNetList[n].source.layers)
                                    continue;
                                minCost = min(minCost,
                                              IroutesDistance(irouteOnTrack, taDB->netList[netIndex].subNetList[n]));
//minCost += x[ir][t] * IroutesDistance(irouteOnTrack,taDB->netList[netIndex].subNetList[n]);
                            }

//pins connect cost
                            for (int n = 0; n < taDB->netList[netIndex].pinList.size(); ++n) {
                                pin = taDB->netList[netIndex].pinList[n];
                                pin.x *= 10;
                                pin.y *= 10;

                                minCost = min(minCost, IroutePinDistance(irouteOnTrack, pin));
//minCost += x[ir][t] * IroutePinDistance(irouteOnTrack,pin);
                            }

                            if (minCost == 1e9)
                                minCost = 0;

                            wlcost[ir][t] += minCost;

                        }
                    }
                }
//TODO Change

//                const int a = subpanel->numofsegments / 32 + 1;
//                vector<vector<int>> dp(subpanel->intervalList.size(), vector<int>(panel->tracks.size(), 0));
//                vector<vector<int>> flag(subpanel->intervalList.size(), vector<int>(panel->tracks.size(), -1));
//                vector<int> o_cost(panel->tracks.size(), 0);
//                vector<vector<vector<unsigned int>>> onums(panel->tracks.size(),
//                                                           vector<vector<unsigned int>>(panel->tracks.size(),
//                                                                                        vector<unsigned int>(a, 0)));
//
//                for (int row = 0; row < subpanel->intervalList.size(); row++) {
//
//                    if (row == 0) {
//                        for (int column = 0; column < panel->tracks.size(); column++) {
//                            dp[0][column] = wlcost[0][column] * wl_cost_weight + blkcost[0][column] * blk_cost_weight;
////                            for (int k = 0; k < subpanel->numofsegments; k++) {
//                            for (int k = c_startToEnd[row].first; k <= c_startToEnd[row].second; k++) {
//                                if (subpanel->c[row][k]) {
//                                    int elementIndex = k / 32;
//                                    int bitIndex = k % 32;
//                                    onums[column][column][elementIndex] |= (1u << bitIndex);
//                                }
//                            }
//                        }
//                    } else {
//                        for (int column = 0; column < panel->tracks.size(); column++) {
//                            int lastCost = 2147483640;
//                            int lastIndex = 0;
//                            for (int t = 0; t < panel->tracks.size(); t++) {
//                                int changeCost = 0;
////                                for (int k = 0; k < subpanel->numofsegments; k++) {
//                                for (int k = c_startToEnd[row].first; k <= c_startToEnd[row].second; k++) {
//                                    int elementIndex = k / 32;
//                                    int bitIndex = k % 32;
//                                    if (subpanel->c[row][k] && ((onums[t][column][elementIndex] & (1u << bitIndex)) != 0)) {
//                                        changeCost += (coord[k + 1 + subpanel->source] - coord[k + subpanel->source]);
//                                    }
//                                }
//                                if (
//                                        changeCost*ol_cost_weight +
//                                        dp[row - 1][t] < lastCost) {
//                                    lastCost =
//                                            changeCost*ol_cost_weight +
//                                            dp[row - 1][t];
//                                    lastIndex = t;
//                                }
//                                else if(changeCost*ol_cost_weight + dp[row - 1][t] == lastCost&& dp[row-1][t]> dp[row-1][lastIndex]){
//                                    lastCost =
//                                            changeCost*ol_cost_weight +
//                                            dp[row - 1][t];
//                                    lastIndex = t;
//                                }
//                            }
//
////                            cout<<"lastCost"<<lastCost<<endl;
//                            flag[row][column] = lastIndex;
////                            cout<<"lastIndex"<<lastIndex<<endl;
//                        }
//
//                        vector<int> copy_o_cost(o_cost);
//                        vector<vector<vector<unsigned int>>> copiedOnums(onums);
//                        for (int j = 0; j < panel->tracks.size(); j++) {
//                            onums[j] = copiedOnums[flag[row][j]];
//                            o_cost[j] = copy_o_cost[flag[row][j]];
//                            int o_cost2 = 0;
//                            for (int k = c_startToEnd[row].first; k <= c_startToEnd[row].second; k++) {
////                            for (int k = 0; k < subpanel->numofsegments; k++) {
//                                int elementIndex = k / 32;
//                                int bitIndex = k % 32;
//                                if (subpanel->c[row][k]) {
//                                    if ((onums[j][j][elementIndex] & (1u << bitIndex)) != 0) {
//                                        o_cost[j] += (coord[k + 1 + subpanel->source] - coord[k + subpanel->source]);
//                                        o_cost2 += (coord[k + 1 + subpanel->source] - coord[k + subpanel->source]);
//                                    }
//                                    onums[j][j][elementIndex] |= (1u << bitIndex);
//                                }
//                            }
//                            dp[row][j] =
//                                    wl_cost_weight * wlcost[row][j] +
//                                    blk_cost_weight * blkcost[row][j]
//                                         + o_cost2*ol_cost_weight + dp[row - 1][flag[row][j]];
//                        }
//                    }
//                }
//                int minIndex = 0;
//
//                for (int j = 0; j < panel->tracks.size(); j++) {
//                    if (dp[subpanel->intervalList.size() - 1][minIndex] > dp[subpanel->intervalList.size() - 1][j]) {
//                        minIndex = j;
//                    }
//                }
//
//                x[subpanel->intervalList.size() - 1]=minIndex;

//                int *x1 = new int[subpanel->intervalList.size()];
//                double* wlcost2 = new double[subpanel->intervalList.size()*panel->tracks.size()];
//                double* blkcost2 = new double[subpanel->intervalList.size()*panel->tracks.size()];
//                for(int ins = 0;ins<subpanel->intervalList.size();ins++){
//                    for(int t = 0 ;t<panel->tracks.size();t++){
//                        wlcost2[ins*panel->tracks.size()+t] = wlcost[ins][t];
//                        blkcost2[ins*panel->tracks.size()+t] = blkcost[ins][t];
//                    }
//                    x1[ins] = -1;
//                }
//
//                int *c_start = new int[subpanel->intervalList.size()];
//                int *c_end = new int[subpanel->intervalList.size()];
//                int *coord2 = new int[subpanel->numofsegments+1];
//                for(int ins = 0 ;ins<subpanel->intervalList.size();ins++){
//                    c_start[ins] = c_startToEnd[ins].first;
//                    c_end[ins] = c_startToEnd[ins].second;
//                }
//                for(int k=0;k<=subpanel->numofsegments;k++){
//                    coord2[k] = coord[subpanel->source+k];
//                }
                vector<int> x(subpanel->intervalList.size(),-1);
//
//                assign_x(x1,panel->tracks.size(),subpanel->intervalList.size(),numofsegments,wlcost2,blkcost2,c_start,c_end,coord2);
//
//                for(int ins =0;ins<subpanel->intervalList.size();ins++){
//                    x[ins] = x1[ins];
//                }

                vector<vector<int>> in_To_track(subpanel->intervalList.size(),vector<int>());
                vector<vector<int>> track_To_in(panel->tracks.size(),vector<int>());
                vector<vector<int>> c_To_in(subpanel->numofsegments,vector<int>());
                vector<vector<int>> c_To_t(subpanel->numofsegments,vector<int>());

                for(int in = 0;in<subpanel->intervalList.size();in++){
                    double min_blk_in_t = DBL_MAX;
                    double min_wl_in_t  = DBL_MAX;
                    vector<int> to_Track;
                    for(int t =0;t<panel->tracks.size();t++){
                        if(blkcost[in][t]==min_blk_in_t){
                            to_Track.push_back(t);
                        }
                        else if(blkcost[in][t]<min_blk_in_t){
                            min_blk_in_t = blkcost[in][t];
                            min_wl_in_t = wlcost[in][t];
                            to_Track.clear();
                            to_Track.push_back(t);
                        }
                    }
                    in_To_track[in]=to_Track;
                    for(int k = c_startToEnd[in].first;k<=c_startToEnd[in].second;k++){
                        for(auto tt:to_Track){
                            if(find(c_To_t[k].begin(),c_To_t[k].end(),tt) == c_To_t[k].end()){
                                c_To_t[k].push_back(tt);
                            }
                        }
                    }
                    for(auto tt:to_Track){
                        track_To_in[tt].push_back(in);
                    }
                }
                for(int ins = 0;ins<subpanel->intervalList.size();ins++){
                    for(int k=c_startToEnd[ins].first;k<=c_startToEnd[ins].second;k++){
                        c_To_in[k].push_back(ins);
                    }
                }

                vector<vector<int>> onums(panel->tracks.size(),vector<int>(subpanel->numofsegments,0));
                vector<vector<int>> in_to_final_track(subpanel->intervalList.size(),vector<int>());
                vector<vector<int>> in_to_bat(subpanel->intervalList.size(),vector<int>());
                for(int ins=0;ins<subpanel->intervalList.size();ins++){
                    for(int ins1=0;ins1<subpanel->intervalList.size();ins1++){
                        if(ins!=ins1){
                            if(c_startToEnd[ins1].second<c_startToEnd[ins].first||
                               c_startToEnd[ins1].first>c_startToEnd[ins].second){
                                in_to_bat[ins].push_back(ins1);
                            }
                        }
                    }
                }

                for(int k=0;k<subpanel->numofsegments;k++){
                    for(auto ins:c_To_in[k]){
                        if(x[ins]!=-1) continue;
                        double min_ov = DBL_MAX;
                        double min_wl = DBL_MAX;
                        int final_track = -1;
                        for(auto t:in_To_track[ins]){
                            double ov = 0;
                            for(int cc=c_startToEnd[ins].first;cc<=c_startToEnd[ins].second;cc++){
                                if(onums[t][cc]>=1){
                                    ov += coord[subpanel->source+cc+1]-coord[subpanel->source+cc];
                                }
                            }
                            if(ov<min_ov){
                                in_to_final_track[ins].clear();
                                min_ov = ov;
                                min_wl = wlcost[ins][t];
                                in_to_final_track[ins].push_back(t);
                                final_track = t;
                            }
                            else if(ov==min_ov&&wlcost[ins][t]<min_wl){
                                in_to_final_track[ins].push_back(t);
                                min_wl =   wlcost[ins][t];
                                final_track  =  t;
                            }
                        }
                        x[ins] = final_track;
                        for(int cc=c_startToEnd[ins].first;cc<=c_startToEnd[ins].second;cc++){
                            onums[x[ins]][cc]++;
                        }
                    }

                }
                int blk_temp = 0;
                int wl_temp = 0;
                int ol_temp = 0;
//                wl_temp += (wlcost[subpanel->intervalList.size() - 1][minIndex]);
//                ol_temp += (o_cost[minIndex]);
//                blk_temp += blkcost[subpanel->intervalList.size() - 1][minIndex];
//
//                for (int in = subpanel->intervalList.size()-1 ; in > 0; in--) {
//                    minIndex = flag[in][minIndex];
//                    x[in - 1] = minIndex;
////                    x[in - 1][minIndex] = 1;
//                    wl_temp += (wlcost[in-1][minIndex] );
//                    blk_temp += blkcost[in-1][minIndex] ;
//                }

                wl_temp = 0;
                blk_temp = 0;
                ol_temp = 0;

                for (int tr = 0; tr < panel->tracks.size(); tr++){
                    vector<bool> overlap2(subpanel->numofsegments, false);
                    for (int irr = 0; irr < subpanel->intervalList.size(); irr++) {
                        if (x[irr] == tr) {
                            for (int s = c_startToEnd[irr].first; s <= c_startToEnd[irr].second; s++) {
//                                    for (int s = 0; s < subpanel->numofsegments; s++) {
                                if (subpanel->c[irr][s] && !overlap2[s]) overlap2[s] = true;
                                else if (subpanel->c[irr][s] && overlap2[s]) {
                                    ol_temp += (coord[s + 1 + subpanel->source] - coord[s + subpanel->source]);
                                }
                            }
                        }
                    }
                }
                for (int in = 0; in < subpanel->intervalList.size(); in++) {
                    int tr = x[in];
                    wl_temp += (wlcost[in][tr]);
                    blk_temp += blkcost[in][tr];
                }


                double wl_alpha =  0.1;
                double ol_alpha = 0.1;
                double bl_beta = 10000;

                for(int p=0;p<0;p++){
                    wl_temp = 0;
                    blk_temp = 0;
                    ol_temp = 0;
                    vector<double> interval_Cost(subpanel->intervalList.size(),0);


                    for(int ir = 0;ir<subpanel->intervalList.size();ir++){
                        int t = x[ir];
//                        for(int t = 0;t<panel->tracks.size();t++){
//                            if(x[ir][t]){
                        interval_Cost[ir] = blk_cost_weight*blkcost[ir][t]+wl_cost_weight*wlcost[ir][t];
                        double changeCost = 0;
                        for (int s = c_startToEnd[ir].first; s <= c_startToEnd[ir].second; s++) {
//                                for(int s=0;s<subpanel->numofsegments;s++){
                            if(subpanel->c[ir][s]) changeCost +=  (coord[s + 1 + subpanel->source] - coord[s + subpanel->source]);
                        }
                        interval_Cost[ir] += changeCost*ol_cost_weight;
//                            }
//                        }
                    }
                    vector<int> ind(subpanel->intervalList.size(),0);
                    for(int ir = 0;ir<subpanel->intervalList.size();ir++){
                        ind[ir] = ir;
                    }
                    sort(ind.begin(),ind.end(),[&](int a,int b)   {
                        return interval_Cost[a]>interval_Cost[b];
                    });


                    for (int ir = 0; ir < subpanel->intervalList.size(); ir++) {
                        int irr = ind[ir];
                        int before_index = x[irr];
                        int before_cost = 0;
//                        for (int t = 0; t < panel->tracks.size(); t++) {
//                            if (x[irr][t]) {
//                                before_index = t;
//                                break;
//                            }
//                        }

//获取其他重叠情况
                        vector<vector<bool>> overlap(panel->tracks.size(),
                                                     vector<bool>(subpanel->numofsegments, false));
                        for (int ir2 = 0; ir2 < subpanel->intervalList.size(); ir2++) {
                            if (ir2 == irr) continue;
                            int t = x[ir2];
//                            for (int t = 0; t < panel->tracks.size(); t++) {
//                                if (x[ir2][t] == 1) {
                            bool mark = false;
                            for (int s = c_startToEnd[ir2].first; s <= c_startToEnd[ir2].second; s++) {
//                                    for (int s = 0; s < subpanel->numofsegments; s++) {
                                if (!subpanel->c[ir2][s] && mark) break;
                                if (subpanel->c[ir2][s]) {
                                    mark = true;
                                    overlap[t][s] = true;
                                }
                            }
//                                    break;
//                                }
//                            }
                        }
                        for (int s = c_startToEnd[irr].first; s <= c_startToEnd[irr].second; s++) {
//                        for (int s = 0; s < subpanel->numofsegments; s++) {
                            if (overlap[before_index][s] && subpanel->c[irr][s])
                                before_cost += (coord[s + 1 + subpanel->source] - coord[s + subpanel->source]);
                        }
                        int new_index = before_index;
                        int new_cost = before_cost;

                        for (int tr = 0; tr < panel->tracks.size(); tr++) {
                            int temp_cost = 0;
                            for (int s = c_startToEnd[irr].first; s <= c_startToEnd[irr].second; s++) {
//                            for (int s = 0; s < subpanel->numofsegments; s++) {
                                if (overlap[tr][s] && subpanel->c[irr][s])
                                    temp_cost += (coord[s + 1 + subpanel->source] - coord[s + subpanel->source]);
                            }
                            if(temp_cost*ol_cost_weight + blkcost[irr][tr] * blk_cost_weight + wlcost[irr][tr] * wl_cost_weight
                               == new_cost*ol_cost_weight + blkcost[irr][new_index] * blk_cost_weight +
                                  wlcost[irr][new_index] * wl_cost_weight){
                                if(temp_cost<new_cost){
                                    new_index = tr;
                                    new_cost = temp_cost;
                                }
                            }
                            else if (temp_cost*ol_cost_weight + blkcost[irr][tr] * blk_cost_weight + wlcost[irr][tr] * wl_cost_weight
                                     < new_cost*ol_cost_weight + blkcost[irr][new_index] * blk_cost_weight +
                                       wlcost[irr][new_index] * wl_cost_weight) {
                                new_index = tr;
                                new_cost = temp_cost;
                            }
                        }
                        x[irr] = new_index;
//                        x[irr][before_index] = false;
//                        x[irr][new_index] = true;
                    }


                    for (int t = 0; t < panel->tracks.size(); t++) {
                        vector<bool> overlap2(subpanel->numofsegments, false);
                        for (int ir = 0; ir < subpanel->intervalList.size(); ir++) {
                            if(x[ir] == t){
//                            if (x[ir][t] == 1) {
                                int t = x[ir];
                                for (int s = c_startToEnd[ir].first; s <= c_startToEnd[ir].second; s++) {
//                                for (int s = 0; s < subpanel->numofsegments; s++) {
                                    if (subpanel->c[ir][s] && !overlap2[s]) overlap2[s] = true;
                                    else if (subpanel->c[ir][s] && overlap2[s]) {
                                        ol_temp += (coord[s + 1 + subpanel->source] - coord[s + subpanel->source]);
                                    }
                                }
                            }
                        }
                    }


                    for (int in = 0; in < subpanel->intervalList.size(); in++) {
//                        for (int t = 0; t < panel->tracks.size(); t++) {
//                            if (x[in][t]) {
                        int t = x[in];
                        wl_temp += (wlcost[in][t]);
                        blk_temp += blkcost[in][t];
//                            }
//                        }
                    }
                }


                for(int p=0;p<0;p++){
                    vector<vector<int>> over_status(panel->tracks.size(),
                                                    vector<int>(subpanel->numofsegments));
                    ol_alpha = 0.1+p*0.1;
                    wl_temp = 0;
                    blk_temp = 0;
                    ol_temp = 0;
                    for(int ir = 0;ir<subpanel->intervalList.size();ir++){
                        int t = x[ir];
//                        for(int t = 0;t<panel->tracks.size();t++){
//                            if(y[ir][t]){
                        for (int s = c_startToEnd[ir].first; s <= c_startToEnd[ir].second; s++) {
//                                for(int s=0;s<subpanel->numofsegments;s++){
                            if(subpanel->c[ir][s]) {
                                over_status[t][s] = over_status[t][s]+1;
                            }
//                                }
//                            }
                        }
                    }


                    for(int ir=0;ir<subpanel->intervalList.size();ir++){
                        double old_overlapCost = 0;
                        double old_blockCost = 0;
                        int old_track = 0;
                        int t = x[ir];
//                        for(int t=0;t<panel->tracks.size();t++){
//                            if(y[ir][t]) {
                        old_blockCost = blkcost[ir][t];
                        old_track = t;
                        for (int k = c_startToEnd[ir].first; k <= c_startToEnd[ir].second; k++) {
//                                for(int k =0;k<subpanel->numofsegments;k++){
                            if(subpanel->c[ir][k]&&over_status[t][k]>=2){
                                old_overlapCost +=
                                        (coord[k + 1 + subpanel->source] - coord[k + subpanel->source]);
                            }
//                                }
//                            }
                        }


                        int new_track = 0;
                        double min_cost = 0;
                        for(int tr = 0; tr < panel->tracks.size(); tr++){

                            double cost = 0;
                            cost = cost + wlcost[ir][tr] * wl_alpha;
                            cost += bl_beta*(blkcost[ir][tr] - blkcost[ir][old_track]);
                            if(tr == old_track){
                                for (int k = c_startToEnd[ir].first; k <= c_startToEnd[ir].second; k++) {
//                                for(int k =0;k<subpanel->numofsegments;k++){
                                    if(subpanel->c[ir][k]&& over_status[tr][k] >= 2){
                                        cost+=ol_alpha*((coord[k + 1 + subpanel->source] - coord[k + subpanel->source]))
                                              +(over_status[tr][k] - 1);
                                    }
                                }
                            }
                            else{
                                for (int k = c_startToEnd[ir].first; k <= c_startToEnd[ir].second; k++) {
//                                for(int k =0;k<subpanel->numofsegments;k++){
                                    if(subpanel->c[ir][k]&& over_status[tr][k] >= 1){
                                        cost+=ol_alpha*((coord[k + 1 + subpanel->source] - coord[k + subpanel->source]))
                                              +(over_status[tr][k]);
                                    }
                                }
                            }
                            cost = cost - ol_alpha*old_overlapCost;

                            if(tr==0) {
                                min_cost = cost;
                                new_track = tr;
                            }
                            if(min_cost>cost){
                                min_cost = cost;
                                new_track = tr;
                            }

                        }


//                        y[ir][old_track] = false;
//                        y[ir][new_track] = true;
                        x[ir] = new_track;
                        for (int k = c_startToEnd[ir].first; k <= c_startToEnd[ir].second; k++) {
//                          for(int k =0;k<subpanel->numofsegments;k++){
                            if(subpanel->c[ir][k]) {
                                over_status[old_track][k] -= 1;
                                over_status[new_track][k] += 1;
                            }
                        }
                    }
                    for (int tr = 0; tr < panel->tracks.size(); tr++) {
                        vector<bool> overlap2(subpanel->numofsegments, false);
                        for (int irr = 0; irr < subpanel->intervalList.size(); irr++) {
                            if (x[irr] == tr) {
                                for (int s = c_startToEnd[irr].first; s <= c_startToEnd[irr].second; s++) {
//                                    for (int s = 0; s < subpanel->numofsegments; s++) {
                                    if (subpanel->c[irr][s] && !overlap2[s]) overlap2[s] = true;
                                    else if (subpanel->c[irr][s] && overlap2[s]) {
                                        ol_temp += (coord[s + 1 + subpanel->source] - coord[s + subpanel->source]);
                                    }
                                }
                            }
                        }
                    }
                    for (int in = 0; in < subpanel->intervalList.size(); in++) {
                        int tr = x[in];
//                            for (int t = 0; t < panel->tracks.size(); t++) {
//                                if (y[in][t]) {
//                                    x[in]=t;
                        wl_temp += (wlcost[in][tr]);
                        blk_temp += blkcost[in][tr];
//                                }
//                            }
                    }
                }






                omp_set_lock(&mylock);
                AssignIrouteToTrack_DP(panel, subpanel, x);
                if (subpanel->remov_intervalList.size() > 0) {
                    printf("subpanel->remov_intervalList.size():%d\n", subpanel->remov_intervalList.size());
                    AssignRemIrouteToTrack(panel, subpanel);
                }
                omp_unset_lock(&mylock);
                omp_set_lock(&mylock);
                total_blkcost+= (blk_temp/10);
                total_olpcost+=(ol_temp/10);
                total_wlcost += wl_temp/10;
                cost11 += ol_temp/10;
                cost22 += blk_temp/10;
                omp_unset_lock(&mylock);
//total_blkcost+=(blk_cost/10);
//total_olpcost+=(overlap_result/10);
//                #pragma omp critical
                if (subpanel->intervalList.size()>=1000) {
                    string str = "output_draw_dp/layer"+ to_string(i)+"panel"+ to_string(l)+"_"+to_string(sub)+".txt";
                    std::ofstream outputFile(str, std::ios::out);
                    for (int in = 0;in<subpanel->intervalList.size();in++) {
                        outputFile<<coord[subpanel->source+c_startToEnd[in].first]<<
                                  " "<<coord[subpanel->source+1+c_startToEnd[in].second]<<" "<<x[in]<<endl;
                    }
                }

                {
                    if (subpanel->intervalList.size()<=100) {
//                        cout << "ocost" << endl;
//                        // 遍历向量并输出每个元約1�7
//                        for (int value: o_cost) {
//                            std::cout << value << " ";
//                        }
//                        std::cout << std::endl;  // 输出换行
                        cout<<"subpanel->intervalList.size()"<<subpanel->intervalList.size();
//                        cout << "xxx" << endl;
                        int a = 0;
//                        cout<<endl<<"cost= "<<final_min_cost_blk<<" + "<<final_min_cost_ol<<" + "<<final_min_cost_wl<<endl<<"xxx";
                        for (const auto &row: x) {
//                            for (auto value: row) {
                            if(a%5==0) printf("\n");
                            a++;
                            printf("%2d ",row);
                        }
                        std::cout << std::endl;  // 每行结束后换衄1�7
//                        }
//                        cout << "flag" << endl;
//                        for (const auto &row: flag) {
//                            for (auto value: row) {
//                                std::cout << value << " ";
//                            }
//                            std::cout << std::endl;  // 每行结束后换衄1�7
//                        }
//                        cout << "dp" << endl;
//                        for (const auto &row: dp) {
//                            for (auto value: row) {
//                                std::cout << value << " ";
//                            }
//                            std::cout << std::endl;  // 每行结束后换衄1�7
//                        }
//                        cout << "blk" << endl;
//                        for (const auto &row: blkcost) {
//                            for (auto value: row) {
//                                std::cout << value << " ";
//                            }
//                            std::cout << std::endl;  // 每行结束后换衄1�7
//                        }
//                        cout << "wl" << endl;
//                        for (const auto &row: wlcost) {
//                            for (auto value: row) {
//                                std::cout << value << " ";
//                            }
//                            std::cout << std::endl;  // 每行结束后换衄1�7
//                        }
//                        cout << "ccc" << endl;
//                        for (int iro = 0; iro < subpanel->intervalList.size(); iro++) {
//                            for (int s = 0; s < subpanel->numofsegments; s++) {
//                                std::cout << subpanel->c[iro][s] << " ";
//                            }
//                            std::cout << std::endl;  // 每行结束后换衄1�7
//                        }
//                        cout << "coord" << endl;
//                        for (int k = 0; k < subpanel->numofsegments; k++) {
//                            cout << coord[k + 1 + subpanel->source] - coord[k + subpanel->source] << " ";
//                        }
//                        cout << endl;
                    }

// 释放 dp 的内孄1�7
//                    for (int i = 0; i < dp.size(); ++i) {
//                        dp[i].clear(); // 清空每个子向量的元素
//                    }
//                    dp.clear(); // 清空主向釄1�7
//
//                    // 释放 flag 的内孄1�7
//                    for (int i = 0; i < flag.size(); ++i) {
//                        flag[i].clear(); // 清空每个子向量的元素
//                    }
//                    flag.clear(); // 清空主向釄1�7
//
//                    // 释放 o_cost 的内孄1�7
//                    o_cost.clear();
//
//                    // 释放 onums 的内孄1�7
//                    for (int i = 0; i < onums.size(); ++i) {
//                        for (int j = 0; j < onums[i].size(); ++j) {
//                            onums[i][j].clear(); // 清空每个子向量的元素
//                        }
//                        onums[i].clear(); // 清空每个子向釄1�7
//                    }
//                    onums.clear(); // 清空主向釄1�7

                    auto end_time = std::chrono::high_resolution_clock::now();
// 计算时间巄1�7
                    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
// 输出运行时间
                    printf("subpanels.intervalist.size():%d\n", subpanel->intervalList.size());
                    cout << "track_size " << panel->tracks.size() << endl;
                    std::cout << "Runtime: " << duration.count() << " ms" << std::endl;
                }
            }

        }
    }


//Update netList
    for (int i=0;i<taDB->irouteList.size();++i)
    {
//local net
        if (taDB->irouteList[i].subNetIndex == -1)
            taDB->netList[taDB->irouteList[i].netIndex].trunk = taDB->irouteList[i];
        else//global net
            taDB->netList[taDB->irouteList[i].netIndex].subNetList[taDB->irouteList[i].subNetIndex] = taDB->irouteList[i];
    }

    printf("The num of cut panel is %d\n",cutpanel);
}

void ILPAssign::DPMethod4(){
    auto start_time = std::chrono::high_resolution_clock::now();

    int cutpanel=0;

    auto& irouteList = taDB->irouteList;
    auto& netList = taDB->netList;

    for (int i=0;i<irouteList.size();++i)
    {
        const auto& iroute = irouteList[i];  // 使用引用减少重复访问
        int subNetIndex = iroute.subNetIndex;
        int netIndex = iroute.netIndex;

        if (subNetIndex == -1) {
            netList[netIndex].trunk = iroute;
        } else {
            netList[netIndex].subNetList[subNetIndex] = iroute;
        }
    }



    auto& trackLayer = taDB->trackLayer;  // 使用引用来减少多次访问
    const auto& tileWidth = taDB->info.tileWidth;   // 提取常量
    const auto& tileHeight = taDB->info.tileHeight; // 提取常量
    Interval tempInterval;
    for (int i=0;i<irouteList.size();++i)
    {
        const auto& iroute = irouteList[i];  // 将常用变量存储到局部变量中
        int layer = iroute.source.layers;
        int netIndex = iroute.netIndex;

        auto& currentTrackLayer = trackLayer[layer];
        auto& panels = currentTrackLayer.panels;

        int panelCoord = currentTrackLayer.isVertical
                         ? GetBinCoord(iroute.source.x, tileWidth)
                         : GetBinCoord(iroute.source.y, tileHeight);

        tempInterval.irouteIndex = i;
        tempInterval.iroutesPinsCount = netList[netIndex].subNetList.size() + netList[netIndex].pinList.size();
        tempInterval.start = iroute.start;
        tempInterval.end = iroute.end;
        panels[panelCoord].intervalList.push_back(tempInterval);
    }
    printf("number of iroute %d\n",irouteList.size());


    auto end_time1 = std::chrono::high_resolution_clock::now();
    auto runtime1 = std::chrono::duration_cast<std::chrono::milliseconds>(end_time1-start_time);



//    omp_lock_t mylock;
//    omp_init_lock(&mylock);
    auto total_duration_segment = std::chrono::milliseconds::zero();
    auto total_duration_cost = std::chrono::milliseconds::zero();
    auto total_duration_assign = std::chrono::milliseconds::zero();


    std::vector<int> mapValue;
    int count;


    for(int i=2;i<taDB->trackLayer.size();++i)//Layer
    {
        omp_set_num_threads(t_num);
        auto start_time22 = std::chrono::high_resolution_clock::now();
        const auto& panels = trackLayer[i].panels;
        mapValue.resize(panels.size());
        count = 0;
        for (const auto& panel :  trackLayer[i].panels)
        {
            mapValue[count] = panel.first; // panelCoord
            ++count; // panel
        }
        int panel_size = panels.size();
        int *tracks_size = new int[panel_size];
        int *interval_size = new int[panel_size];
        int *segments_size = new int[panel_size];
        memset(tracks_size, 0, panel_size * sizeof(int));
        memset(interval_size, 0, panel_size * sizeof(int));
        memset(segments_size, 0, panel_size * sizeof(int));

        int *coord_begin = new int [panel_size];

        int *ins_begin = new int[panel_size];

        int *cost_begin = new int[panel_size];

        int *onums_begin = new int[panel_size];

        int  *track_begin = new int[panel_size];

        vector<vector<int>> host_coord(taDB->trackLayer[i].panels.size(),vector<int>());
        // ins
//        vector<vector<int>> host_ins(taDB->trackLayer[i].panels.size(),vector<int>());
//        vector<vector<int>> host_ins1(taDB->trackLayer[i].panels.size(),vector<int>());
//        vector<vector<int>> host_c_start(taDB->trackLayer[i].panels.size(),vector<int>());
//        vector<vector<int>> host_c_end(taDB->trackLayer[i].panels.size(),vector<int>());
//        vector<vector<int>> host_x(taDB->trackLayer[i].panels.size(),vector<int>());

        cout << " layer:" << i << endl;
        int ins_size = 0;
        int all_track_size = 0;
        int max_track_size = 0;
        int solution_count = 0;
        int *solution_begin = new int [panel_size];
        for(int l=0;l<panel_size;++l){
            Panel *panel = &trackLayer[i].panels[mapValue[l]];
            const auto& intervalList = panel->intervalList;
            const auto& tracks = panel->tracks;
            if(l==0) {
                ins_begin[l] =0 ;
                track_begin[l]=0;
            }
            else {
                ins_begin[l] =ins_begin[l-1]+interval_size[l-1];
                track_begin[l] = track_begin[l-1]+tracks_size[l-1];
            }
            ins_size += intervalList.size();
            all_track_size += tracks.size();
            interval_size[l] = intervalList.size();
            tracks_size[l] = tracks.size();
            max_track_size = max(max_track_size,tracks_size[l]);
        }
        solution_count = ins_size*max_track_size*max_track_size;
        for(int l=0;l<panel_size;++l){
            solution_begin[l] = ins_begin[l]*max_track_size*max_track_size;
        }
        int *inss = new int[ins_size];
        int *inss1 = new int[ins_size];

        #pragma omp parallel for schedule(dynamic,1)
        for(int l=0;l<panel_size;++l){
            Panel *panel = &trackLayer[i].panels[mapValue[l]];
            const auto& intervalList = panel->intervalList;
            int i_begin = ins_begin[l];
            for(int in = 0;in<intervalList.size();in++){
                int a = i_begin+in;
                inss[a] = in;
                inss1[a] = in;
            }
            if(intervalList.empty()) continue;
            std::stable_sort(inss + i_begin, inss + i_begin + intervalList.size(), [&](int a, int b) {
                return intervalList[a].start < intervalList[b].start ||
                       (intervalList[a].start == intervalList[b].start && intervalList[a].end < intervalList[b].end);
            });

            std::stable_sort(inss1 + i_begin, inss1 + i_begin + intervalList.size(), [&](int a, int b) {
                return intervalList[a].end < intervalList[b].end ||
                       (intervalList[a].end == intervalList[b].end && intervalList[a].start < intervalList[b].start);
            });

        }

        int *c_start = new int[ins_size];
        int *c_end = new int[ins_size];

        #pragma omp parallel for schedule(dynamic,1)
        for(int l=0;l<panel_size;++l)//panel,
        {
            Panel *panel = &trackLayer[i].panels[mapValue[l]];
            Interval interval;
            const auto& intervalList = panel->intervalList;
            std::vector<int> coord(intervalList.size() * 2);
            int cnt = 0;
            for (const auto& interval : intervalList) {
                coord[cnt++] = interval.start;
                coord[cnt++] = interval.end;
            }

            std::set<int> unique_coords(coord.begin(), coord.end());
            segments_size[l] = unique_coords.size();
            std::unordered_map<int, int> coord_index;
            int index = 0;
            for (const int& val : unique_coords) {
                coord_index[val] = index++;
            }
            host_coord[l].assign(unique_coords.begin(), unique_coords.end());
            int ib = ins_begin[l];
            for (int ins = 0; ins < intervalList.size(); ++ins) {
                int start = coord_index[intervalList[ins].start];
                int end = coord_index[intervalList[ins].end] - 1;
                int ib_in = ib+ins;
                c_start[ib_in] = start;
                c_end[ib_in] = end;
            }
        }

        auto start_cost_time = std::chrono::high_resolution_clock::now();
        auto duration11 = std::chrono::duration_cast<std::chrono::milliseconds>(start_cost_time-start_time22);
        total_duration_segment += duration11;

        int layer_size = trackLayer.size();
        int *layer_Vertical = new int[layer_size];
        for(int l=0;l<layer_size;l++){
            layer_Vertical[l] = trackLayer[l].isVertical;
        }


        int *track_coord = new int [all_track_size];
        int *track_blk_begin = new int[all_track_size];
        int *track_blk_size = new int[all_track_size];
        int all_blk_size = 0;
        int cost_size = 0;


        int prev_coord = 0;
        int prev_onums = 0;
        int prev_track_begin = 0;
        int pre_ins = 0;
        int prev_track_blk_begin = 0;
        for(int l=0;l<panel_size;l++){
            Panel& panel = trackLayer[i].panels[mapValue[l]];
            cost_begin[l] = cost_size;

            cost_size += interval_size[l]*tracks_size[l];
            coord_begin[l] = prev_coord;
            onums_begin[l] = prev_onums;
            track_begin[l] = prev_track_begin;
            ins_begin[l] = pre_ins;

            prev_coord += segments_size[l];
            pre_ins += interval_size[l];
            prev_onums += segments_size[l]*tracks_size[l];
            prev_track_begin += tracks_size[l];

            int track_beginl = track_begin[l];
            for(int t=0;t<panel.tracks.size();t++){
                track_blk_begin[track_beginl+t] = prev_track_blk_begin;
                int tb_tmp = panel.tracks[t].blk.size();
                prev_track_blk_begin += panel.tracks[t].blk.size();
                const auto& blk = panel.tracks[t].blk;
                all_blk_size += tb_tmp;
                track_blk_size[track_beginl+t] = tb_tmp;
            }
        }


        int last_panel_index = taDB->trackLayer[i].panels.size()-1;
        int coord_size = coord_begin[last_panel_index]+host_coord[last_panel_index].size();
        int onums_size = onums_begin[last_panel_index]+(segments_size[last_panel_index])*tracks_size[last_panel_index];
        int *cost_To_Panel = new int[cost_size];



        int *blk_start = new int [all_blk_size];
        int *blk_end = new int[all_blk_size];
        int *interval_start = new int[ins_size];
        int *interval_end = new int [ins_size];
        #pragma omp parallel for schedule(dynamic,1)
        for(int l=0;l<panel_size;l++) {
            Panel& panel = trackLayer[i].panels[mapValue[l]];
            int a = track_begin[l];
            const auto& tracks = panel.tracks;
            const auto& intervalList = panel.intervalList;
            for (int t = 0; t < tracks.size(); t++) {
                int b = track_blk_begin[a+t];
                auto &blks = tracks[t].blk;
                for (int tb = 0; tb < blks.size(); tb++) {
                    blk_start[b+tb] = blks[tb].start;
                    blk_end[b+tb] = blks[tb].end;
                }
            }
            int costb = cost_begin[l];
            for(int in=0;in<intervalList.size();in++){
                auto& inte = intervalList[in];
                interval_start[ins_begin[l]+in] = inte.start;
                interval_end[ins_begin[l]+in] = inte.end;
                auto track_size = tracks_size[l];
                auto cp_begin = costb+in*track_size;
                for(int t =0;t<track_size;t++){
                    auto cpt = cp_begin+t;
                    cost_To_Panel[cpt] = l;
                }
            }
        }
        int* wlcost1 = new int[cost_size];
        int* blkcost1 = new int[cost_size];
        #pragma omp parallel
        {
            #pragma omp sections
            {
                #pragma omp section
                {
                    #pragma omp parallel for
                    for(int a = 0; a < cost_size; ++a) {
                        blkcost1[a] = 0;
                    }
                }

                #pragma omp section
                {
                    #pragma omp parallel for
                    for(int a = 0; a < cost_size; ++a) {
                        wlcost1[a] = 0;
                    }
                }
            }
        }


        int *coord = new int [coord_size];
        int *x = new int[ins_size];
        std::fill(x,x+ins_size,-1);
        int *olcost = new int[cost_size];
        std::fill(olcost, olcost + cost_size, INT_MAX);
        printf("cost_Size %d\n",cost_size);

        for(int l=0;l<panel_size;l++){
            int coob = coord_begin[l];
            int ib = ins_begin[l];
            for(int s=0;s<segments_size[l];s++){
                coord[coob+s] = host_coord[l][s];
            }
        }

//        calculate_blkcost(blkcost1,blk_start,blk_end,interval_start,interval_end,
//                          track_blk_begin,track_blk_size,tracks_size,interval_size,
//                          taDB->trackLayer[i].panels.size(),cost_begin,cost_size,all_blk_size,ins_size,all_track_size,cost_To_Panel,
//                          ins_begin,track_begin);

        omp_set_num_threads(t_num);

        #pragma omp parallel for schedule(dynamic, 1)
        for(int a=0;a<cost_size;a++){
            int current_panel = cost_To_Panel[a];
            int i_size = interval_size[current_panel];
            int t_size = tracks_size[current_panel];
            if(t_size==0||i_size==0) continue;
            int indexInPanel = a-cost_begin[current_panel];
            int ins_index = indexInPanel/t_size;
            int t_index = indexInPanel%t_size;
            int blk_cost_sum = 0;
            int blkOverlap = 0;
            int tb = track_begin[current_panel];
            int ib = ins_begin[current_panel];
            int ib_inindex = ib+ins_index;
            int tb_tindex = tb+t_index;
            if(track_blk_size[tb_tindex] ==0 ) continue;
            int in_end = interval_end[ib_inindex];
            int in_start = interval_start[ib_inindex];
            for(int ii=track_blk_begin[tb_tindex]; ii < track_blk_begin[tb_tindex] + track_blk_size[tb_tindex]; ii++){

                int min_value = in_end < blk_end[ii] ? in_end : blk_end[ii];
                int max_value = in_start > blk_start[ii] ? in_start : blk_start[ii];
                blkOverlap = min_value-max_value;
                if(blkOverlap>0)
                    blk_cost_sum += blkOverlap;
            }
            blkcost1[a] = blk_cost_sum;
        }



        unordered_set<int> unique_netIndex;



        for(int l=0;l<panel_size;l++) {
            Panel *panel = &trackLayer[i].panels[mapValue[l]];
            const auto& intervalList = panel->intervalList;
            for(auto in : intervalList){
                int irouteIndex = in.irouteIndex;
                int netIndex = irouteList[irouteIndex].netIndex;
                unique_netIndex.insert(netIndex);
            }
        }

        vector<int> uni_vec(unique_netIndex.begin(),unique_netIndex.end());
        int ns = uni_vec.size();

        int *subNettt_begin = new int[ns];
        int *subNet_size = new int[ns];
        int subNettt_size_sum = 0;
        int *pin_size = new int[ns];
        int *pin_begin = new int[ns];
        int pin_size_sum = 0;


        #pragma omp parallel sections
        {
            #pragma omp section
            std::fill(pin_size, pin_size + ns, 0);

            #pragma omp section
            std::fill(subNet_size, subNet_size + ns, 0);

            #pragma omp section
            std::fill(pin_begin, pin_begin + ns, 0);

            #pragma omp section
            std::fill(subNettt_begin, subNettt_begin + ns, 0);
        }

        // 并行计算 subNet_size 和 pin_size
        #pragma omp parallel for
        for (int unii = 0; unii < ns; ++unii) {
            auto net = netList[uni_vec[unii]];
            subNet_size[unii] = net.subNetList.size();
            pin_size[unii] = net.pinList.size();
        }

        // 计算 subNettt_begin 和 pin_begin 的增量
        subNettt_begin[0] = 0;
        pin_begin[0] = 0;
        for (int unii = 1; unii < ns; ++unii) {
            subNettt_begin[unii] = subNettt_begin[unii - 1] + subNet_size[unii - 1];
            pin_begin[unii] = pin_begin[unii - 1] + pin_size[unii - 1];
        }
        subNettt_size_sum = subNettt_begin[ns - 1] + subNet_size[ns - 1];
        pin_size_sum = pin_begin[ns - 1] + pin_size[ns - 1];


        Coordinate3D1 *pin_coord = new Coordinate3D1[pin_size_sum];
        Iroutttt *subNettt = new Iroutttt[subNettt_size_sum];


        #pragma omp parallel for schedule(dynamic,1)
        for (int unii = 0; unii < ns; ++unii) {
            const auto& net = netList[uni_vec[unii]];
            int net_begin = subNettt_begin[unii];
            int pb = pin_begin[unii];

            auto* local_subNettt = subNettt + net_begin;
            auto* local_pin_coord = pin_coord + pb;
            for (int si = 0; si < subNet_size[unii]; ++si) {
                const auto& subNet = net.subNetList[si];
                local_subNettt[si].source.x = subNet.source.x;
                local_subNettt[si].source.y = subNet.source.y;
                local_subNettt[si].source.layers = subNet.source.layers;
                local_subNettt[si].target.x = subNet.target.x;
                local_subNettt[si].target.y = subNet.target.y;
                local_subNettt[si].target.layers = subNet.target.layers;
                local_subNettt[si].start = subNet.start;
                local_subNettt[si].end = subNet.end;
                local_subNettt[si].assigned = subNet.assigned;
            }
            for (int pi = 0; pi < net.pinList.size(); ++pi) {
                const auto& pin = net.pinList[pi];
                local_pin_coord[pi].x = pin.x;
                local_pin_coord[pi].y = pin.y;
                local_pin_coord[pi].layers = pin.layers;
            }
        }


        unordered_map<int, int> index_map;
        index_map.reserve(uni_vec.size());  // 预分配空间，避免动态扩展
        for(int unii = 0; unii < uni_vec.size(); unii++) {
            index_map[uni_vec[unii]] = unii;
        }
        printf("subNettt_size_sum%d  \nns  %d\n",subNettt_size_sum,ns);

        int *net_Index = new int[ins_size];
        int *subnet_Index = new int[ins_size];
        Coordinate3D1 *source = new Coordinate3D1[ins_size];
        #pragma omp parallel for schedule(dynamic,1)
        for(int l=0;l<panel_size;l++){
            Panel *panel = &trackLayer[i].panels[mapValue[l]];
            const auto& intervalList = panel->intervalList;
            for(int k=0;k<panel->tracks.size();k++){
                track_coord[track_begin[l]+k]  = panel->tracks[k].coordinate;
            }
            int count = 0;
            for (const auto& interval : intervalList) {
                int irouteIndex = interval.irouteIndex;
                int netIndex = irouteList[irouteIndex].netIndex;
                int subNetIndex = irouteList[irouteIndex].subNetIndex;
                int ins_begin_in = ins_begin[l] + count;
                count ++;
                if (index_map.find(netIndex) != index_map.end()) {
                    net_Index[ins_begin_in] = index_map[netIndex];
                } else {
                    printf("error not found\n");
                }
                subnet_Index[ins_begin_in] = subNetIndex;
                const auto& trunk_source = netList[netIndex].trunk.source;
                if (subNetIndex == -1) {
                    source[ins_begin_in].x = trunk_source.x;
                    source[ins_begin_in].y = trunk_source.y;
                    source[ins_begin_in].layers = trunk_source.layers;
                } else {

                }
            }

        }





        #pragma omp parallel for schedule(dynamic,1)
        for(int ci=0;ci<cost_size;ci++){
            int tid = ci;
            int current_panel = cost_To_Panel[tid];
            int i_size = interval_size[current_panel];
            int t_size = tracks_size[current_panel];
            int indexInPanel = tid-cost_begin[current_panel];

            int ins_index = indexInPanel/t_size;
            int t_index = indexInPanel%t_size;
            int minCost = 0;
            wlcost1[tid]=0;
            int lll = 0;
            int ins = ins_begin[current_panel]+ins_index;

            int irouteIndex ,netIndex,subNetIndex;
            auto interval = taDB->trackLayer[i].panels[mapValue[current_panel]].intervalList[ins_index];
            irouteIndex = interval.irouteIndex;
            netIndex = taDB->irouteList[irouteIndex].netIndex;
            subNetIndex = taDB->irouteList[irouteIndex].subNetIndex;

            int tt = track_begin[current_panel]+t_index;
            Coordinate3D1 trunk = source[ins];
            int ni=net_Index[ins];


            if(subnet_Index[ins] == -1){
                if(layer_Vertical[trunk.layers]){
                    wlcost1[tid] = abs(trunk.x-track_coord[tt]);
                }
                else {
                    wlcost1[tid] = abs(trunk.y-track_coord[tt]);
                }

            }
            else {
                int sub_begin =  subNettt_begin[ni];

                Iroutttt irouteOnTrack = subNettt[sub_begin + subnet_Index[ins]];
                Iroute irouteOnTrack1 = taDB->netList[netIndex].subNetList[subNetIndex];
                Coordinate3D1 pin;
                int source_layer = irouteOnTrack.source.layers;
                if(layer_Vertical[source_layer]){
                    int minCost = 1e9;
                    irouteOnTrack.source.x = irouteOnTrack.target.x = track_coord[tt];
                    int begin = subNettt_begin[ni];
                    int lll = 0;
                    for(int j=0;j<subNet_size[ni];j++) {
                        if (!subNettt[begin+j].assigned) continue;
                        if (subNettt[begin + subnet_Index[ins]].source.layers == subNettt[begin + j].source.layers) continue;
                        Iroute irouteOnTrack1 ;


                        irouteOnTrack1.source.x = irouteOnTrack.source.x;
                        irouteOnTrack1.source.y = irouteOnTrack.source.y;
                        irouteOnTrack1.source.layers = irouteOnTrack.source.layers;
                        irouteOnTrack1.target.x = irouteOnTrack.target.x;
                        irouteOnTrack1.target.y = irouteOnTrack.target.y;
                        irouteOnTrack1.target.layers = irouteOnTrack.target.layers;
                        irouteOnTrack1.start = irouteOnTrack.start;
                        irouteOnTrack1.end = irouteOnTrack.end;
                        Iroute subn;
                        subn.source.x = subNettt[begin + j].source.x;
                        subn.source.y = subNettt[begin + j].source.y;
                        subn.source.layers = subNettt[begin + j].source.layers;
                        subn.target.x = subNettt[begin + j].target.x;
                        subn.target.y = subNettt[begin + j].target.y;
                        subn.target.layers = subNettt[begin + j].target.layers;
                        subn.start = subNettt[begin + j].start;
                        subn.end = subNettt[begin + j].end;
                        int distance = IroutesDistance(irouteOnTrack1,subn);
                        int distance2 = IroutesDistance(irouteOnTrack1,taDB->netList[netIndex].subNetList[j]);
                        if(distance<minCost) {
                            minCost = distance;
                            lll=j;
                        }
                        minCost = min(minCost,
                                      IroutesDistance(irouteOnTrack1,subn));
                    }

                    for(int n=0;n<pin_size[ni];n++){
                        pin = pin_coord[pin_begin[ni]+n];
                        Coordinate3D pin2 = taDB->netList[netIndex].pinList[n];
                        pin.x*=10;
                        pin.y*=10;
                        pin2.x*=10;
                        pin2.y*=10;
                        Iroute irouteOnTrack1 ;
                        irouteOnTrack1.source.x = irouteOnTrack.source.x;
                        irouteOnTrack1.source.y = irouteOnTrack.source.y;
                        irouteOnTrack1.source.layers = irouteOnTrack.source.layers;
                        irouteOnTrack1.target.x = irouteOnTrack.target.x;
                        irouteOnTrack1.target.y = irouteOnTrack.target.y;
                        irouteOnTrack1.target.layers = irouteOnTrack.target.layers;
                        irouteOnTrack1.start = irouteOnTrack.start;
                        irouteOnTrack1.end = irouteOnTrack.end;
                        Coordinate3D pin1;
                        pin1.x = pin.x;
                        pin1.y = pin.y;
                        pin1.layers = pin.layers;
                        int distance = IroutePinDistance(irouteOnTrack1,pin1);
                        int distance2 = IroutePinDistance(irouteOnTrack1,pin2);
                        if(distance<minCost) {
                            minCost = distance;
                            lll = 0-n;
                        }
                        minCost = min(minCost, IroutePinDistance(irouteOnTrack1,pin1));
                    }
                    if(minCost==1e9) minCost = 0;
                    wlcost1[tid] += minCost;
                }
                else {

                    int minCost = 1e9;
                    irouteOnTrack.source.y = irouteOnTrack.target.y = track_coord[tt];
                    int begin = subNettt_begin[ni];

                    for(int j=0;j<subNet_size[ni];j++) {
                        if (!subNettt[begin+j].assigned) continue;
                        if (subNettt[begin + subnet_Index[ins]].source.layers == subNettt[begin + j].source.layers) continue;
                        Iroute irouteOnTrack1 ;
                        irouteOnTrack1.source.x = irouteOnTrack.source.x;
                        irouteOnTrack1.source.y = irouteOnTrack.source.y;
                        irouteOnTrack1.source.layers = irouteOnTrack.source.layers;
                        irouteOnTrack1.target.x = irouteOnTrack.target.x;
                        irouteOnTrack1.target.y = irouteOnTrack.target.y;
                        irouteOnTrack1.target.layers = irouteOnTrack.target.layers;
                        irouteOnTrack1.start = irouteOnTrack.start;
                        irouteOnTrack1.end = irouteOnTrack.end;
                        Iroute subn;
                        subn.source.x = subNettt[begin + j].source.x;
                        subn.source.y = subNettt[begin + j].source.y;
                        subn.source.layers = subNettt[begin + j].source.layers;
                        subn.target.x = subNettt[begin + j].target.x;
                        subn.target.y = subNettt[begin + j].target.y;
                        subn.target.layers = subNettt[begin + j].target.layers;
                        subn.start = subNettt[begin + j].start;
                        subn.end = subNettt[begin + j].end;
                        int distance = IroutesDistance(irouteOnTrack1,subn);
                        if(distance<minCost) {
                            minCost = distance;
                            lll=j;
                        }

                        minCost = min(minCost,
                                      IroutesDistance(irouteOnTrack1,subn));

                    }
                    for(int n=0;n<pin_size[ni];n++){
                        pin = pin_coord[pin_begin[ni]+n];
                        pin.x*=10;
                        pin.y*=10;
                        Iroute irouteOnTrack1 ;
                        irouteOnTrack1.source.x = irouteOnTrack.source.x;
                        irouteOnTrack1.source.y = irouteOnTrack.source.y;
                        irouteOnTrack1.source.layers = irouteOnTrack.source.layers;
                        irouteOnTrack1.target.x = irouteOnTrack.target.x;
                        irouteOnTrack1.target.y = irouteOnTrack.target.y;
                        irouteOnTrack1.target.layers = irouteOnTrack.target.layers;
                        irouteOnTrack1.start = irouteOnTrack.start;
                        irouteOnTrack1.end = irouteOnTrack.end;
                        Coordinate3D pin1;
                        pin1.x = pin.x;
                        pin1.y = pin.y;
                        pin1.layers = pin.layers;
                        int distance = IroutePinDistance(irouteOnTrack1,pin1);
                        if(distance<minCost) {
                            minCost = distance;
                            lll = 0-n;
                        }

                        minCost = min(minCost, IroutePinDistance(irouteOnTrack1,pin1));
                    }
                    if(minCost==1e9) minCost = 0;
                    wlcost1[tid] += minCost;
                }
            }
        }


//        calculate_cost1(wlcost1, net_Index, source, layer_Vertical, track_coord
//                , subNet_size, subNettt_begin, pin_size, pin_begin, pin_size_sum
//                , pin_coord
//                , ins_size, layer_size, cost_begin, cost_size, all_track_size
//                , interval_size, tracks_size, taDB->trackLayer[i].panels.size(), cost_To_Panel, ins_begin, track_begin, subnet_Index
//                , subNettt, ns, subNettt_size_sum
////                ,wlcost
//        );

        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_cost_time);
        total_duration_cost += duration;


        auto s_time1 = std::chrono::high_resolution_clock::now();


        int wl_tmp = 0;
        int blk_tmp = 0;
        int ol_tmp = 0;

        if(true){
            int *subpanel_ins_begin = new int[ins_size];
            int *subpanel_ins_end = new int[ins_size];
            int *subpanel_size = new int[panel_size];


            // 初始化内存（相当于 cudaMemset）
            std::fill(subpanel_ins_begin, subpanel_ins_begin + ins_size, 0);
            std::fill(subpanel_ins_end, subpanel_ins_end + ins_size, 0);
            std::fill(subpanel_size, subpanel_size + panel_size, 0);



            int *onums = new int[onums_size];
            memset(onums, 0, onums_size * sizeof(int));


            // 为其他额外数组分配内存
            int *subsubpanel_ins_begin = new int[ins_size];
            int *subsubpanel_ins_end = new int[ins_size];
            int *subsubpanel_size = new int[ins_size];
            int *subsubpanel_To_subpanel = new int[ins_size];
            int *subsubpanel_begin_in_subpanel = new int[ins_size];
            int *subsubpanel_end_in_subpanel = new int[ins_size];

            std::fill(subsubpanel_ins_begin, subsubpanel_ins_begin + ins_size, 0);
            std::fill(subsubpanel_ins_end, subsubpanel_ins_end + ins_size, 0);
            std::fill(subsubpanel_size, subsubpanel_size + ins_size, 0);
            std::fill(subsubpanel_To_subpanel, subsubpanel_To_subpanel + ins_size, 0);
            std::fill(subsubpanel_begin_in_subpanel, subsubpanel_begin_in_subpanel + ins_size, 0);
            std::fill(subsubpanel_end_in_subpanel, subsubpanel_end_in_subpanel + ins_size, 0);

            #pragma omp parallel for schedule(dynamic, 1)
            for (int tid = 0; tid < panel_size; ++tid) {
                if (interval_size[tid] == 0) continue;

                int tmp_subsubpanel_size = 0;
                int last_subsubpanel_right = 0;

                int tmp_subpanel_size = 0;
                int most_right = 0;

                for (int index = 0; index < interval_size[tid]; ++index) {
                    int in = inss[ins_begin[tid] + index];
                    if (index == 0) {
                        subpanel_ins_begin[ins_begin[tid] + tmp_subpanel_size] = index;
                        most_right = c_end[ins_begin[tid] + in];
//                        subsubpanel_begin_in_subpanel[ins_begin[tid] + tmp_subpanel_size] = tmp_subsubpanel_size;
//                        subsubpanel_ins_begin[ins_begin[tid] + tmp_subsubpanel_size] = index;
//                        last_subsubpanel_right = c_start[ins_begin[tid] + in] - 1;
                    } else {
                        // subpanel
                        if (c_start[ins_begin[tid] + in] > most_right) {
//                            subsubpanel_ins_end[ins_begin[tid] + tmp_subsubpanel_size] = index - 1;
//                            subsubpanel_To_subpanel[ins_begin[tid] + tmp_subsubpanel_size] = tmp_subpanel_size;
//                            if (tmp_subsubpanel_size >= 1) {
//                                int ind = subsubpanel_ins_end[ins_begin[tid] + tmp_subsubpanel_size - 1];
//                                int inn = inss[ins_begin[tid] + ind];
//                                last_subsubpanel_right = c_end[ins_begin[tid] + inn];
//                            }
//                            subsubpanel_end_in_subpanel[ins_begin[tid] + tmp_subpanel_size] = tmp_subsubpanel_size;
//
//                            tmp_subsubpanel_size++;
//                            subsubpanel_ins_begin[ins_begin[tid] + tmp_subsubpanel_size] = index;

                            subpanel_ins_end[ins_begin[tid] + tmp_subpanel_size] = index - 1;
                            subsubpanel_To_subpanel[ins_begin[tid] + tmp_subsubpanel_size] = tmp_subpanel_size;

                            tmp_subpanel_size++;
//                            subsubpanel_begin_in_subpanel[ins_begin[tid] + tmp_subpanel_size] = tmp_subsubpanel_size;
                            subpanel_ins_begin[ins_begin[tid] + tmp_subpanel_size] = index;
                            most_right = c_end[ins_begin[tid] + in];
                        } else {
                            if (c_end[ins_begin[tid] + in] > most_right) {
                                most_right = c_end[ins_begin[tid] + in];
                                if (c_start[ins_begin[tid] + in] >= last_subsubpanel_right) {
//                                    subsubpanel_ins_end[ins_begin[tid] + tmp_subsubpanel_size] = index - 1;
//                                    if (tmp_subsubpanel_size >= 1) {
//                                        int ind = subsubpanel_ins_end[ins_begin[tid] + tmp_subsubpanel_size - 1];
//                                        int inn = inss[ins_begin[tid] + ind];
//                                        last_subsubpanel_right = c_end[ins_begin[tid] + inn];
//                                    }
//                                    subsubpanel_To_subpanel[ins_begin[tid] + tmp_subsubpanel_size] = tmp_subpanel_size;
//
//                                    tmp_subsubpanel_size++;
//                                    subsubpanel_ins_begin[ins_begin[tid] + tmp_subsubpanel_size] = index;
                                }
                            }
                        }
                    }
                    if (index == interval_size[tid] - 1) {
//                        subsubpanel_ins_end[ins_begin[tid] + tmp_subsubpanel_size] = index;
//                        subsubpanel_end_in_subpanel[ins_begin[tid] + tmp_subpanel_size] = tmp_subsubpanel_size;
//                        tmp_subsubpanel_size++;
//
//                        subsubpanel_To_subpanel[ins_begin[tid] + tmp_subsubpanel_size] = tmp_subpanel_size;
                        subpanel_ins_end[ins_begin[tid] + tmp_subpanel_size] = index;
                        tmp_subpanel_size++;
                    }
                }

                subpanel_size[tid] = tmp_subpanel_size;
                subsubpanel_size[tid] = tmp_subsubpanel_size;
            }


            int all_subpanel_size = 0;
            for(int ii = 0; ii < panel_size; ii++) {
                all_subpanel_size += subpanel_size[ii];
            }

            int all_subsubpanel_size = 0;
            for(int ii = 0; ii < ins_size; ii++) {
                all_subsubpanel_size += subsubpanel_size[ii];
            }
            std::cout << "all_subsubpanel_size " << all_subsubpanel_size << std::endl;
            std::cout << "ins_size " << ins_size << std::endl;

            std::cout << "all_subpanel_size " << all_subpanel_size << std::endl;
            std::vector<int> subpanel_to_panel_id(all_subpanel_size);
            std::vector<int> subpanel_begin(panel_size);
            int current_index = 0;

            int* host_subsubpanel_To_panel_id = new int[all_subsubpanel_size];
            int* host_subsubpanel_begin = new int[panel_size];
            int current_index1 = 0;

            int* host_subsubpanel_index_begin = new int[all_subpanel_size];
            int max_track_size = 0;
            int subsubpanel_count = 0;
            int subpanel_count = 0;
            int max_ins_size_in_subpaenl = 0;

            for(int ii = 0; ii < panel_size; ii++) {
                if(tracks_size[ii] > max_track_size) max_track_size = tracks_size[ii];

                int panel_ins_begin = ins_begin[ii];

                subpanel_begin[ii] = current_index;
                host_subsubpanel_begin[ii] = current_index1;
                for(int j = 0; j < subpanel_size[ii]; j++) {
                    host_subsubpanel_index_begin[subpanel_count] = subsubpanel_count;
                    subsubpanel_count += (subsubpanel_end_in_subpanel[panel_ins_begin + j] -
                                          subsubpanel_begin_in_subpanel[panel_ins_begin + j] + 1);
                    subpanel_count++;
                    subpanel_to_panel_id[current_index] = ii;
                    current_index++;
                }

                for(int j = 0; j < subsubpanel_size[ii]; j++) {
                    host_subsubpanel_To_panel_id[current_index1] = ii;
                    current_index1++;
                }
            }
            std::vector<int> insToPanel(ins_size);
            for (int a = 0; a < panel_size; a++) {
                for (int b = 0; b < interval_size[a]; b++) {
                    insToPanel[ins_begin[a] + b] = a;
                }
            }

            std::vector<double> fake_x(cost_size, 0.0); // 代替cudaMemset
            std::vector<double> fake_x_tmp(cost_size, 0.0); // 代替cudaMemset
            #pragma omp parallel for schedule(dynamic, 1)
            for (int tid = 0; tid < ins_size; ++tid) {
                int current_panel = insToPanel[tid];
                int i_size = interval_size[current_panel];
                int t_size = tracks_size[current_panel];
                if (t_size == 0 || i_size == 0) continue;

                int ins_index1 = tid - ins_begin[current_panel];
                int ins_index = inss1[ins_begin[current_panel] + ins_index1];
                int min_blkcost = INT_MAX;
                double min_blkcost_count = 0;

                // Finding the minimum block cost
                for (int t = 0; t < t_size; ++t) {
                    int cost_idx = cost_begin[current_panel] + ins_index * t_size + t;
                    if (blkcost1[cost_idx] < min_blkcost) {
                        min_blkcost = blkcost1[cost_idx];
                        min_blkcost_count = 1.0;
                    } else if (blkcost1[cost_idx] == min_blkcost) {
                        min_blkcost_count=  min_blkcost_count+1.0;
                    }
                }

                // Assigning fake_x based on the minimum block cost
                for (int t = 0; t < t_size; ++t) {
                    fake_x_tmp[cost_begin[current_panel]+ins_index*t_size+t] = 0.0;
                    int cost_idx = cost_begin[current_panel] + ins_index * t_size + t;
                    if (blkcost1[cost_idx] == min_blkcost) {
                        fake_x[cost_idx] = 1.0 / min_blkcost_count;
                    } else {
                        fake_x[cost_idx] = 0.0;
                    }
                }
            }
            int *ov_size = new int[ins_size];
            int ov_size_sum = 0;
            std::vector<int> ov_begin(ins_size, 0);

            #pragma omp parallel for schedule(dynamic, 1)
            for (int tid = 0; tid < ins_size; ++tid) {
                int current_panel = insToPanel[tid];
                int i_size = interval_size[current_panel];
                int t_size = tracks_size[current_panel];
                if (t_size == 0 || i_size == 0) continue;

                int ins_index = tid - ins_begin[current_panel];
                int ins_index1 = inss1[ins_begin[current_panel] + ins_index];
                int left1 = c_start[ins_begin[current_panel] + ins_index1];
                int right1 = c_end[ins_begin[current_panel] + ins_index1];
                int count1 = 0;

                for (int ii = ins_index - 1; ii >= 0; --ii) {
                    int ins = inss1[ins_begin[current_panel] + ii];
                    int left2 = c_start[ins_begin[current_panel] + ins];
                    int right2 = c_end[ins_begin[current_panel] + ins];

                    if (left1 <= right2) {
                        int overlap = std::min(right1, right2) - std::max(left1, left2) + 1;
                        if (overlap >= 1) count1++;
                    } else {
                        break;
                    }
                }
                ov_size[ins_begin[current_panel] + ins_index1] = count1;
            }

            for(int a =0;a<ins_size;a++) {
                if(a==0) ov_begin[a] = 0;
                else {
                    ov_begin[a] = ov_begin[a-1]+ov_size[a-1];
                }
                ov_size_sum += ov_size[a];
            }

            std::vector<int> ov_length(ov_size_sum,0);
            std::vector<int> ov_to_ins_index(ov_size_sum,0);
            #pragma omp parallel for schedule(dynamic, 1)
            for (int tid = 0; tid < ins_size; ++tid) {
                int current_panel = insToPanel[tid];
                int i_size = interval_size[current_panel];
                int t_size = tracks_size[current_panel];
                if (t_size == 0 || i_size == 0) continue;

                int ins_index = tid - ins_begin[current_panel];
                int ins_index1 = inss1[ins_begin[current_panel] + ins_index];
                int left1 = c_start[ins_begin[current_panel] + ins_index1];
                int right1 = c_end[ins_begin[current_panel] + ins_index1];
                int count1 = 0;
                int ov_start_idx = ov_begin[ins_begin[current_panel] + ins_index1];

                for (int ii = ins_index - 1; ii >= 0; --ii) {
                    int ins = inss1[ins_begin[current_panel] + ii];
                    int left2 = c_start[ins_begin[current_panel] + ins];
                    int right2 = c_end[ins_begin[current_panel] + ins];

                    if (left1 <= right2) {
                        int min_right = std::min(right1, right2);
                        int max_left = std::max(left1, left2);
                        int overlap = min_right - max_left + 1;

                        if (overlap >= 1) {
                            ov_length[ov_start_idx + count1] = coord[coord_begin[current_panel] + min_right + 1] - coord[coord_begin[current_panel] + max_left];
                            ov_to_ins_index[ov_start_idx + count1] = ins;
                            count1++;
                        }
                    } else {
                        break;
                    }
                }
            }

            std::cout << "ov_size_sum: " << ov_size_sum << std::endl;

            vector<int> solution_cost(cost_size,0);
            vector<int> solution_boundry(solution_count,-1);
            vector<int> solution_final_last_track(cost_size,0);
            vector<int> solution_final_track_mark(cost_size,0);
            

            std::mutex mtx;
            #pragma omp parallel for schedule(dynamic, 1)
            for (int tid1 = 0; tid1 < all_subpanel_size; tid1++) {
                int panel_id = subpanel_to_panel_id[tid1];
                int tid = tid1 - subpanel_begin[panel_id];
                int panel_ins_begin = ins_begin[panel_id];
                int sub_ins_end = subpanel_ins_end[panel_ins_begin + tid];
                int sub_ins_begin = subpanel_ins_begin[panel_ins_begin + tid];
                int ib = ins_begin[panel_id];
                int ob = onums_begin[panel_id];
                int coordb = coord_begin[panel_id];
                int costb = cost_begin[panel_id];
                int seg_size = segments_size[panel_id];
                int tr_size = tracks_size[panel_id];


                for (int ii = 0; ii < 1; ii++) {
                    for (int a = sub_ins_begin; a <= sub_ins_end; a++) {
                        int ins = inss1[ins_begin[panel_id] + a];
                        double value_sum = 0.0;
                        double max_value = 0.0;

                        for (int tt = 0; tt < tracks_size[panel_id]; tt++) {
                            int cost_index = costb+ins*tr_size + tt;
                            double value = 0.0;

                            if (fake_x[cost_index] == 0.0) {
                                fake_x_tmp[cost_index] = 0.0;
                                continue;
                            }


                            for (int b = 0; b < ov_size[ib + ins]; b++) {
                                int to_ins = ov_to_ins_index[ov_begin[ib+ins] + b];
                                double ov_length1 = ov_length[ov_begin[ib+ins] + b];
                                int to_cost_index = costb+to_ins*tr_size + tt;
                                value +=
                                        fake_x[to_cost_index]
                                        *(ov_length1+fake_x_tmp[to_cost_index])
                                        ;
                            }

                            if(value>max_value) max_value = value;
                            fake_x_tmp[cost_index] = value;

                        }

                        for(int tt = 0; tt < tracks_size[panel_id]; tt++){
                            int cost_index = costb+ins*tr_size + tt;
                            double tmp = fake_x_tmp[cost_index];
                            value_sum += ( max_value - tmp);
                        }

                        double alpha = 0.5;

                        for (int tt = 0; tt < tracks_size[panel_id]; tt++) {
                            int cost_index = costb+ins*tr_size + tt;
                            if (fake_x[cost_index] == 0.0) {
                                continue;
                            }
                            if (value_sum != 0.0) {
                                double tmp = (fake_x[cost_index] * alpha +
                                              (1 - alpha) * (max_value-fake_x_tmp[cost_index]) / value_sum);
                                fake_x[cost_index] = tmp;
                            }
                        }
                    }
                }
                int blk_tmp1 = 0, wl_tmp1 = 0, ol_tmp1 = 0;


                int m_track_size = max_track_size;
                int sl_begin = solution_begin[panel_id];
                /*for (int a = sub_ins_end; a >= sub_ins_begin; a--) {
                    int ins = inss1[ib + a];
                    int insInAll = panel_ins_begin + ins;
                    int ib_ins = ib + ins;
                    int costb_ins_tr_size = costb + ins * tr_size;
                    int min_ov = INT_MAX;
                    int min_wl = INT_MAX;
                    int min_blk = INT_MAX;
                    int final_track = -1;

                    if (x[ib_ins] >= 0) {
                        continue;
                    }

                    for (int tt = 0; tt < tr_size; ++tt) {
                        int costb_ins_trs_tt = costb_ins_tr_size + tt;
                        int ov1 = 0;
                        for (int cc = c_start[insInAll]; cc <= c_end[insInAll]; ++cc) {
                            if (onums[ob + tt * (seg_size) + cc] >= 1) {
                                ov1 += (coord[coordb + cc + 1] - coord[coordb + cc]);
                            }
                        }
                        olcost[costb_ins_trs_tt] = ov1;
                        if (ov1 < min_ov)
                            min_ov = ov1;
                        if (blkcost1[costb_ins_trs_tt] < min_blk)
                            min_blk = blkcost1[costb_ins_trs_tt];
                    }

                    bool flag = true;
                    int tmp_ov = INT_MAX;
                    int tmp_wl = INT_MAX;
                    double max_fake_x = 0.0;

                    for (int tt = 0; tt < tr_size; ++tt) {
                        int costb_ins_trs_tt = costb_ins_tr_size + tt;
                        if(blkcost1[costb_ins_trs_tt] == min_blk &&
                           olcost[costb_ins_trs_tt] == min_ov){
                            if(fake_x[costb_ins_trs_tt]>max_fake_x){
                                max_fake_x = fake_x[costb_ins_trs_tt];
                                tmp_wl = wlcost1[costb_ins_trs_tt];
                                tmp_ov = olcost[costb_ins_trs_tt];
                                final_track = tt;
                            }
                            else if(fake_x[costb_ins_trs_tt]==max_fake_x) {
                                if (wlcost1[costb_ins_trs_tt] < tmp_wl) {
                                    tmp_wl = wlcost1[costb_ins_trs_tt];
                                    final_track = tt;
                                }
                            }
                            if(flag) flag = false;

                        }
                        else if(
                                blkcost1[costb_ins_trs_tt] == min_blk &&
                                olcost[costb_ins_trs_tt] > min_ov){
                            if(flag){
                                if(olcost[costb_ins_trs_tt] < tmp_ov) {
                                    tmp_ov = olcost[costb + ins * tr_size + tt];
                                    tmp_wl = wlcost1[costb + ins * tr_size + tt];
                                    final_track = tt;
                                }
                                else if(olcost[costb_ins_trs_tt] == tmp_ov
                                        && wlcost1[costb_ins_trs_tt] < tmp_wl){
                                    tmp_wl = wlcost1[costb_ins_trs_tt];
                                    final_track = tt;
                                }
                            }
                        }
                    }

                    int derta_wl = 0;
                    if(flag)
                    {
                        //没有blkcost和olcost同时取到最小值的状况
                        //那么检查olcost取到最小值的轨道段是否能与blkcost取到最小值的轨道段进行交换，因为障碍物与轨道是绑定的。
                        bool iscanchange = false;
                        int blk_track = -1;
                        int ol_track = -1;
                        int from_index = -1;

                        int most_de = 0;


                        for(int tt=0;tt<tr_size;tt++){
                            int costb_ins_trs_tt = costb_ins_tr_size + tt;
                            if(blkcost1[costb_ins_tr_size+tt]==min_blk){
                                //此时tt所在轨道应该是有障碍物所以重叠成本不能取到最小值
                                for(int tt1=0;tt1<tr_size;tt1++){
                                    int costb_ins_trs_tt1 = costb_ins_tr_size + tt1;
                                    if(tt==tt1) continue;
                                    if(olcost[costb_ins_trs_tt1]<tmp_ov){
                                        bool isCanChange11 = false;
                                        int derta_blk = 0;
                                        int derta_ov = 0;
//                            for(int aa=a-1;aa>=sub_ins_begin;aa--){
//                                int ins1 = inss[ib+aa];
                                        for(int aa=a+1;aa<=sub_ins_end;aa++){
                                            int ins1 = inss1[ib+aa];
                                            int ib_ins1 = ib+ins1;
                                            int costb_ins1_trs_tt = costb+ins1*tr_size+tt;
                                            int costb_ins1_trs_tt1 = costb+ins1*tr_size+tt1;
                                            if(x[ib_ins1]!=tt1&&x[ib_ins1]!=tt) continue;
                                            if(x[ib_ins1]==tt1){
                                                derta_blk += (blkcost1[costb_ins1_trs_tt]-blkcost1[costb_ins1_trs_tt1]);
                                                for(int cc = c_start[ib_ins1];cc<=c_end[ib_ins1];cc++) {
                                                    int ob_tt1_seg_size_cc = ob+tt1*(seg_size)+cc;
                                                    int ov_tt_seg_size_cc = ob+tt*(seg_size)+cc;
                                                    if(onums[ov_tt_seg_size_cc]>=1){
                                                        derta_ov += (coord[coordb + cc + 1] - coord[coordb + cc]);
                                                    }
                                                    if(onums[ob_tt1_seg_size_cc]>1){
                                                        derta_ov -= (coord[coordb + cc + 1] - coord[coordb + cc]);
                                                    }
                                                    onums[ov_tt_seg_size_cc] +=1;
                                                    onums[ob_tt1_seg_size_cc] -=1;
                                                }
                                            }
                                            else if(x[ib_ins1]==tt) {
                                                derta_blk += (blkcost1[costb_ins1_trs_tt1]-blkcost1[costb_ins1_trs_tt]);
                                                for(int cc = c_start[ib_ins1];cc<=c_end[ib_ins1];cc++) {
                                                    int ob_tt1_seg_size_cc = ob+tt1*(seg_size)+cc;
                                                    int ov_tt_seg_size_cc = ob+tt*(seg_size)+cc;
                                                    if(onums[ob_tt1_seg_size_cc]>=1){
                                                        derta_ov += (coord[coordb + cc + 1] - coord[coordb + cc]);
                                                    }
                                                    if(onums[ov_tt_seg_size_cc]>1){
                                                        derta_ov -= (coord[coordb + cc + 1] - coord[coordb + cc]);
                                                    }
                                                    onums[ob_tt1_seg_size_cc] +=1;
                                                    onums[ov_tt_seg_size_cc] -=1;
                                                }
                                            }
                                            if(derta_blk==0&&derta_ov==0) {
                                                from_index = aa;
                                                isCanChange11 = true;
                                                break;
                                            }
                                        }
//                            if(!isCanChange11) from_index = sub_ins_begin;
//                            for(int aa=from_index;aa<a;aa++){
//                                int ins1 = inss[ib+aa];
                                        if(!isCanChange11) from_index = sub_ins_end;
                                        for(int aa=from_index;aa>a;aa--){
                                            int ins1 = inss1[ib+aa];
                                            int ib_ins1 = ib+ins1;
                                            if(x[ib_ins1]!=tt1&&x[ib_ins1]!=tt) continue;
                                            if(x[ib_ins1]==tt1){
                                                for(int cc = c_start[ib_ins1];cc<=c_end[ib_ins1];cc++) {
                                                    int ob_tt1_seg_size_cc = ob+tt1*(seg_size)+cc;
                                                    int ov_tt_seg_size_cc = ob+tt*(seg_size)+cc;
                                                    onums[ob_tt1_seg_size_cc] +=1;
                                                    onums[ov_tt_seg_size_cc] -=1;
                                                }
                                            }
                                            else if(x[ib_ins1]==tt) {
                                                for(int cc = c_start[ib_ins1];cc<=c_end[ib_ins1];cc++) {
                                                    int ob_tt1_seg_size_cc = ob+tt1*(seg_size)+cc;
                                                    int ov_tt_seg_size_cc = ob+tt*(seg_size)+cc;
                                                    onums[ov_tt_seg_size_cc ] +=1;
                                                    onums[ob_tt1_seg_size_cc] -=1;
                                                }
                                            }
                                        }

                                        if(isCanChange11&&most_de<tmp_ov-olcost[costb_ins_trs_tt1]) {
                                            ol_track = tt1;
                                            iscanchange = true;
                                            most_de = tmp_ov-olcost[costb_ins_trs_tt1];
//                                break;
                                            if (most_de == tmp_ov-olcost[costb_ins_trs_tt1]) {
                                                iscanchange = true;
                                                break;
                                            }
                                        }
//                            if(iscanchange){
//                                ol_track = tt1;
//                                break;
//                            }
                                    }
                                }
                                if(iscanchange){
                                    blk_track = tt;
                                    break;
                                }
                            }
                        }
                        if(iscanchange){
                            final_track = blk_track;
//                for(int aa=from_index;aa<a;aa++){
//                    int ins1 = inss[ib+aa];
                            for(int aa=from_index;aa>a;aa--){
                                int ins1 = inss1[ib+aa];
                                int ib_ins1 = ib+ins1;
                                int costb_ins1_trs_blkt = costb+ins1*tr_size+blk_track;
                                int costb_ins1_trs_olt = costb+ins1*tr_size+ol_track;
                                if(x[ib_ins1]==ol_track){
                                    x[ib_ins1] = blk_track;
                                    derta_wl+=(wlcost1[costb_ins1_trs_blkt]
                                               -wlcost1[costb_ins1_trs_olt]);
                                    for(int cc = c_start[ib_ins1];cc<=c_end[ib_ins1];cc++){
                                        onums[ob+blk_track*(seg_size)+cc] +=1;
                                        onums[ob+ol_track*(seg_size)+cc] -=1;
                                    }
                                }
                                else if(x[ib_ins1]==blk_track){
                                    x[ib_ins1] = ol_track;
                                    derta_wl+=(wlcost1[costb_ins1_trs_olt]
                                               -wlcost1[costb_ins1_trs_blkt]);
                                    for(int cc = c_start[ib_ins1];cc<=c_end[ib_ins1];cc++){
                                        onums[ob+blk_track*(seg_size)+cc] -=1;
                                        onums[ob+ol_track*(seg_size)+cc] +=1;
                                    }
                                }
                            }
                        }
                    }
                    x[ib_ins] = final_track;

                    int ol_tmp2 = 0;
                    for(int cc = c_start[ib_ins];cc<=c_end[ib_ins];cc++){
                        int ob_final_track_seg_size_cc = ob+final_track*(seg_size)+cc;
                        if(onums[ob_final_track_seg_size_cc]>=1){
                            ol_tmp2 += (coord[coordb + cc + 1] - coord[coordb + cc]);
                        }
                        onums[ob_final_track_seg_size_cc] +=1 ;
                    }
                    blk_tmp1 += min_blk;
                    wl_tmp1 += (wlcost1[costb_ins_tr_size+final_track]+derta_wl);
                    ol_tmp1 += ol_tmp2;
                }*/

                int dp_index = sub_ins_begin;
                int solution_be = solution_begin[panel_id];
                int now_index;
                int last_index;
                int error_count = 0;
                bool is_exchange_in_subpanel = false;
                for(int a=sub_ins_end;a>=sub_ins_begin;a--){
                    int ins = inss1[ib+a];
                    int insInAll = panel_ins_begin+ins;
                    int ib_ins = ib+ins;
                    int costb_ins_tr_size = costb+ins*tr_size;
                    int min_ov = INT_MAX;
                    int min_wl = INT_MAX;
                    int min_blk = INT_MAX;
                    int now_solution_be = (ib+a)*max_track_size*max_track_size;
                    if(now_solution_be>=solution_count){
                        printf("ERROR %d %d %d %d!\n",ib,a,max_track_size,solution_count);
                    }
                    bool flag = false;
                    if(a==sub_ins_end){
                        for(int tt = 0;tt<tr_size;tt++) {
                            int costb_ins_trs_tt = costb_ins_tr_size + tt;

                            int solution_be_track = now_solution_be + tt * max_track_size + tt;
                            int cs = c_start[insInAll];
                            solution_boundry[solution_be_track] = coord[coordb + cs];
                            solution_cost[costb_ins_trs_tt] =
                                    solution_cost[costb_ins_trs_tt] + blkcost1[costb_ins_trs_tt] * 1000;
                        }
                    }
                    else {
                        int ins1 = inss1[ib + a + 1];
                        int costb_ins_tr_size1 = costb + ins1 * tr_size;
                        int solution_be_last = (ib + a + 1) * max_track_size * max_track_size;

                        int min_olcost = INT_MAX;
                        int min_blkcost = INT_MAX;

                        int cs = c_start[insInAll];
                        int ce = c_end[insInAll];
                        for (int tt = 0; tt < tr_size; tt++) {
                            int final_cost = INT_MAX;
                            int final_track = -1;

                            double max_fake_x = 0.0;
                            int tmp_wlcost = INT_MAX;

                            int now_olcost = 0;
                            int now_blkcost = blkcost1[costb_ins_tr_size + tt];


                            for (int tt1 = 0; tt1 < tr_size; tt1++) {
                                int costb_ins_trs_tt1 = costb_ins_tr_size1 + tt1;
                                int tmp_cost = solution_cost[costb_ins_trs_tt1];

                                int tmp_olcost = 0;
                                int solution_be_track1 = solution_be_last + tt1 * max_track_size + tt;
                                if (solution_boundry[solution_be_track1] != -1 &&
                                    solution_boundry[solution_be_track1] < coord[coordb + ce + 1]) {
                                    tmp_olcost += (coord[coordb + ce + 1] -
                                                   max(solution_boundry[solution_be_track1], coord[coordb + cs]));
                                }

                                tmp_cost += tmp_olcost;

                                if (final_cost > tmp_cost) {
                                    final_track = tt1;
                                    final_cost = tmp_cost;
                                    now_olcost = tmp_olcost;
                                    tmp_wlcost = wlcost1[costb_ins_trs_tt1];
                                    max_fake_x = fake_x[costb_ins_trs_tt1];
                                } else if (final_cost == tmp_cost) {
                                    if (fake_x[costb_ins_trs_tt1] > max_fake_x) {
                                        final_track = tt1;
                                        tmp_wlcost = wlcost1[costb_ins_trs_tt1];
                                        now_olcost = tmp_olcost;
                                        max_fake_x = fake_x[costb_ins_trs_tt1];
                                    } else if (fake_x[costb_ins_trs_tt1] == max_fake_x && wlcost1[costb_ins_trs_tt1] < tmp_wlcost) {
                                        final_track = tt1;
                                        now_olcost = tmp_olcost;
                                        tmp_wlcost = wlcost1[costb_ins_trs_tt1];
                                    }
                                }

                            }

                            min_olcost = min(min_olcost, now_olcost);
                            min_blkcost = min(min_blkcost, blkcost1[costb_ins_tr_size + tt]);


                            int solution_be_final_track = solution_be_last + final_track * max_track_size;
                            solution_final_last_track[costb_ins_tr_size + tt] = final_track;
                            solution_cost[costb_ins_tr_size + tt] = final_cost + blkcost1[costb_ins_tr_size + tt] * 1000;

                        }



                        for (int tt = 0; tt < tr_size; tt++) {
                            int final_track = solution_final_last_track[costb_ins_tr_size + tt];
                            int now_olcost = 0;
                            int solution_be_track1 = solution_be_last + final_track * max_track_size + tt;
                            if (solution_boundry[solution_be_track1] != -1 &&
                                solution_boundry[solution_be_track1] < coord[coordb + ce + 1]) {
                                now_olcost += (coord[coordb + ce + 1] -
                                               max(solution_boundry[solution_be_track1], coord[coordb + cs]));
                            }
                            int now_blkcost = blkcost1[costb_ins_tr_size + tt];
                            if (now_olcost == min_olcost && min_blkcost == now_blkcost) {
                                flag = true;
                                break;
                            }
                        }

                        for(int tt = 0;tt<tr_size;tt++) {
                            int solution_be_track = now_solution_be + tt*max_track_size;
                            int solution_be_final_track = solution_be_last + solution_final_last_track[costb_ins_tr_size+tt]*max_track_size;
                            for(int tt2 = 0;tt2<tr_size;tt2++){
                                solution_boundry[solution_be_track+tt2] = solution_boundry[solution_be_final_track+tt2];
                            }
                            int update_solution_index = solution_be + a *max_track_size*max_track_size + tt*max_track_size+tt;

                            if(solution_boundry[update_solution_index]!= -1){
                                solution_boundry[update_solution_index] = min(solution_boundry[update_solution_index],coord[coordb+cs]);
                            }
                            else if(solution_boundry[update_solution_index]==-1){
                                solution_boundry[update_solution_index] = coord[coordb+cs];
                            }
                        }


                        if(!flag){
                            dp_index = a+1;
                            error_count++;
                            break;
                        }
                    }
                }


                int final_track = 0;
                for (int a = dp_index; a <= sub_ins_end; a++) {
                    int ins = inss1[ib+a];
                    int insInAll = panel_ins_begin+ins;
                    int ib_ins = ib+ins;
                    int costb_ins_tr_size = costb+ins*tr_size;
                    if(a==dp_index){
                        int min_cost = INT_MAX;
                        int final_wlcost = INT_MAX;
                        for(int tt=0;tt<tr_size;tt++){
                            if(min_cost>solution_cost[costb_ins_tr_size+tt]){
                                min_cost = solution_cost[costb_ins_tr_size+tt];
                                final_wlcost = wlcost1[costb_ins_tr_size+tt];
                                final_track = tt;
                            }
                            else if(min_cost==solution_cost[costb_ins_tr_size+tt]&&wlcost1[costb_ins_tr_size+tt]<final_wlcost){
                                min_cost = solution_cost[costb_ins_tr_size+tt];
                                final_wlcost = wlcost1[costb_ins_tr_size+tt];
                                final_track = tt;
                            }
                        }
                        x[ib_ins] = final_track;
                        blk_tmp1 += blkcost1[costb_ins_tr_size+final_track];
                        wl_tmp1 += wlcost1[costb_ins_tr_size+final_track];
                        int ol_tmp2 = 0;
                        for(int cc = c_start[ib_ins];cc<=c_end[ib_ins];cc++){
                            int ob_final_track_seg_size_cc = ob+final_track*(seg_size)+cc;
                            if(onums[ob_final_track_seg_size_cc]>=1){
                                ol_tmp2 += (coord[coordb + cc + 1] - coord[coordb + cc]);
                            }
                            onums[ob_final_track_seg_size_cc] +=1 ;
                        }
                        ol_tmp1 += ol_tmp2;
                        final_track = solution_final_last_track[costb_ins_tr_size+final_track];
                    }
                    else {
                        int last_ins_track = final_track;

                        int ins1 = inss1[ib+a];
                        int insInAll1 = panel_ins_begin+ins1;
                        int ib_ins1 = ib+ins1;
                        x[ib_ins1] = last_ins_track;
                        int costb_ins_tr_size1 = costb+ins1*tr_size;
                        blk_tmp1 += blkcost1[costb_ins_tr_size1+last_ins_track];
                        wl_tmp1 += wlcost1[costb_ins_tr_size1+last_ins_track];
                        int ol_tmp11 = 0;
                        for(int cc = c_start[ib_ins1];cc<=c_end[ib_ins1];cc++){
                            int ob_final_track_seg_size_cc = ob+last_ins_track*(seg_size)+cc;
                            if(onums[ob_final_track_seg_size_cc]>=1){
                                ol_tmp11 += (coord[coordb + cc + 1] - coord[coordb + cc]);
                            }
                            onums[ob_final_track_seg_size_cc] +=1 ;
                        }
                        ol_tmp1 += ol_tmp11;
                        final_track = solution_final_last_track[costb_ins_tr_size+final_track];
                    }
                }

                for(int a=dp_index;a>=sub_ins_begin;a--) {
                    int ins = inss1[ib+a];
                    int insInAll = panel_ins_begin+ins;
                    int ib_ins = ib+ins;
                    int costb_ins_tr_size = costb+ins*tr_size;
                    int min_ov = INT_MAX;
                    int min_wl = INT_MAX;
                    int min_blk = INT_MAX;
                    int final_track1 = -1;


                    if(x[ib_ins]>=0){
                        continue;
                    }


                    for(int tt=0;tt<tr_size;tt++){
                        int costb_ins_trs_tt = costb_ins_tr_size + tt;
                        int ov1 = 0;
                        for(int cc=c_start[insInAll];cc<=c_end[insInAll];cc++){
                            if(onums[ob+tt*(seg_size)+cc]>=1){
                                ov1+=(coord[coordb + cc + 1] - coord[coordb + cc]);
                            }
                        }
                        olcost[costb_ins_trs_tt] = ov1;
                        if(ov1<min_ov)
                            min_ov = ov1;
                        if(blkcost1[costb_ins_trs_tt]<min_blk)
                            min_blk = blkcost1[costb_ins_trs_tt];
                    }
                    bool flag = true;
                    int tmp_ov = INT_MAX;
                    int tmp_wl = INT_MAX;
                    double max_fake_x = 0.0;
                    for(int tt=0;tt<tr_size;tt++){
                        int costb_ins_trs_tt = costb_ins_tr_size + tt;
                        if(blkcost1[costb_ins_trs_tt] == min_blk &&
                           olcost[costb_ins_trs_tt] == min_ov){

//                        tmp_ov = olcost[costb_ins_trs_tt];
//                        tmp_wl = wlcost[costb_ins_trs_tt];
//                        final_track1 = tt;

                            if(fake_x[costb_ins_trs_tt]>max_fake_x){
                                max_fake_x = fake_x[costb_ins_trs_tt];
                                tmp_wl = wlcost1[costb_ins_trs_tt];
                                tmp_ov = olcost[costb_ins_trs_tt];
                                final_track1 = tt;
                            }
                            else if(fake_x[costb_ins_trs_tt]==max_fake_x) {
                                if (wlcost1[costb_ins_trs_tt] < tmp_wl) {
                                    tmp_wl = wlcost1[costb_ins_trs_tt];
                                    final_track1 = tt;
                                }
                            }
                            if(flag) flag = false;
                        }
                        else if(
                                blkcost1[costb_ins_trs_tt] == min_blk &&
                                olcost[costb_ins_trs_tt] > min_ov){
                            if(flag){
                                if(olcost[costb_ins_trs_tt] < tmp_ov) {
                                    tmp_ov = olcost[costb + ins * tr_size + tt];
                                    tmp_wl = wlcost1[costb + ins * tr_size + tt];
                                    final_track1 = tt;
                                }
                                else if(olcost[costb_ins_trs_tt] == tmp_ov
                                        && wlcost1[costb_ins_trs_tt] < tmp_wl){
                                    tmp_wl = wlcost1[costb_ins_trs_tt];
                                    final_track1 = tt;
                                }
                            }
                        }
                    }
                    int derta_wl = 0;
//        if(false)
                    if(flag)
                    {
                        //没有blkcost和olcost同时取到最小值的状况
                        //那么检查olcost取到最小值的轨道段是否能与blkcost取到最小值的轨道段进行交换，因为障碍物与轨道是绑定的 ?
                        bool iscanchange = false;
                        int blk_track = -1;
                        int ol_track = -1;
                        int from_index = -1;

                        int most_de = 0;


                        for(int tt=0;tt<tr_size;tt++){
                            int costb_ins_trs_tt = costb_ins_tr_size + tt;
                            if(blkcost1[costb_ins_tr_size+tt]==min_blk){
                                //此时tt所在轨道应该是有障碍物所以重叠成本不能取到最小 ?
                                for(int tt1=0;tt1<tr_size;tt1++){
                                    int costb_ins_trs_tt1 = costb_ins_tr_size + tt1;
                                    if(tt==tt1) continue;
                                    if(olcost[costb_ins_trs_tt1]<tmp_ov){
                                        bool isCanChange11 = false;
                                        int derta_blk = 0;
                                        int derta_ov = 0;
//                            for(int aa=a-1;aa>=sub_ins_begin;aa--){
//                                int ins1 = inss[ib+aa];
                                        for(int aa=a+1;aa<=sub_ins_end;aa++){
                                            int ins1 = inss1[ib+aa];
                                            int ib_ins1 = ib+ins1;
                                            int costb_ins1_trs_tt = costb+ins1*tr_size+tt;
                                            int costb_ins1_trs_tt1 = costb+ins1*tr_size+tt1;
                                            if(x[ib_ins1]!=tt1&&x[ib_ins1]!=tt) continue;
                                            if(x[ib_ins1]==tt1){
                                                derta_blk += (blkcost1[costb_ins1_trs_tt]-blkcost1[costb_ins1_trs_tt1]);
                                                for(int cc = c_start[ib_ins1];cc<=c_end[ib_ins1];cc++) {
                                                    int ob_tt1_seg_size_cc = ob+tt1*(seg_size)+cc;
                                                    int ov_tt_seg_size_cc = ob+tt*(seg_size)+cc;
                                                    if(onums[ov_tt_seg_size_cc]>=1){
                                                        derta_ov += (coord[coordb + cc + 1] - coord[coordb + cc]);
                                                    }
                                                    if(onums[ob_tt1_seg_size_cc]>1){
                                                        derta_ov -= (coord[coordb + cc + 1] - coord[coordb + cc]);
                                                    }
                                                    onums[ov_tt_seg_size_cc] +=1;
                                                    onums[ob_tt1_seg_size_cc] -=1;
                                                }
                                            }
                                            else if(x[ib_ins1]==tt) {
                                                derta_blk += (blkcost1[costb_ins1_trs_tt1]-blkcost1[costb_ins1_trs_tt]);
                                                for(int cc = c_start[ib_ins1];cc<=c_end[ib_ins1];cc++) {
                                                    int ob_tt1_seg_size_cc = ob+tt1*(seg_size)+cc;
                                                    int ov_tt_seg_size_cc = ob+tt*(seg_size)+cc;
                                                    if(onums[ob_tt1_seg_size_cc]>=1){
                                                        derta_ov += (coord[coordb + cc + 1] - coord[coordb + cc]);
                                                    }
                                                    if(onums[ov_tt_seg_size_cc]>1){
                                                        derta_ov -= (coord[coordb + cc + 1] - coord[coordb + cc]);
                                                    }
                                                    onums[ob_tt1_seg_size_cc] +=1;
                                                    onums[ov_tt_seg_size_cc] -=1;
                                                }
                                            }
                                            if(derta_blk==0&&derta_ov==0) {
                                                from_index = aa;
                                                isCanChange11 = true;
                                                break;
                                            }
                                        }
//                            if(!isCanChange11) from_index = sub_ins_begin;
//                            for(int aa=from_index;aa<a;aa++){
//                                int ins1 = inss[ib+aa];
                                        if(!isCanChange11) from_index = sub_ins_end;
                                        for(int aa=from_index;aa>a;aa--){
                                            int ins1 = inss1[ib+aa];
                                            int ib_ins1 = ib+ins1;
                                            if(x[ib_ins1]!=tt1&&x[ib_ins1]!=tt) continue;
                                            if(x[ib_ins1]==tt1){
                                                for(int cc = c_start[ib_ins1];cc<=c_end[ib_ins1];cc++) {
                                                    int ob_tt1_seg_size_cc = ob+tt1*(seg_size)+cc;
                                                    int ov_tt_seg_size_cc = ob+tt*(seg_size)+cc;
                                                    onums[ob_tt1_seg_size_cc] +=1;
                                                    onums[ov_tt_seg_size_cc] -=1;
                                                }
                                            }
                                            else if(x[ib_ins1]==tt) {
                                                for(int cc = c_start[ib_ins1];cc<=c_end[ib_ins1];cc++) {
                                                    int ob_tt1_seg_size_cc = ob+tt1*(seg_size)+cc;
                                                    int ov_tt_seg_size_cc = ob+tt*(seg_size)+cc;
                                                    onums[ov_tt_seg_size_cc ] +=1;
                                                    onums[ob_tt1_seg_size_cc] -=1;
                                                }
                                            }
                                        }

                                        if(isCanChange11&&most_de<tmp_ov-olcost[costb_ins_trs_tt1]) {
                                            ol_track = tt1;

                                            iscanchange = true;
                                            most_de = tmp_ov-olcost[costb_ins_trs_tt1];
//                                break;
                                            if (most_de == tmp_ov-olcost[costb_ins_trs_tt1]) {
                                                iscanchange = true;
                                                break;
                                            }
                                        }
//                            if(iscanchange){
//                                ol_track = tt1;
//                                break;
//                            }
                                    }
                                }
                                if(iscanchange){
                                    blk_track = tt;
                                    break;
                                }
                            }
                        }
                        if(iscanchange){
                            final_track1 = blk_track;
//                for(int aa=from_index;aa<a;aa++){
//                    int ins1 = inss[ib+aa];
                            for(int aa=from_index;aa>a;aa--){
                                int ins1 = inss1[ib+aa];
                                int ib_ins1 = ib+ins1;
                                int costb_ins1_trs_blkt = costb+ins1*tr_size+blk_track;
                                int costb_ins1_trs_olt = costb+ins1*tr_size+ol_track;
                                if(x[ib_ins1]==ol_track){
                                    x[ib_ins1] = blk_track;
                                    derta_wl+=(wlcost1[costb_ins1_trs_blkt]
                                               -wlcost1[costb_ins1_trs_olt]);
                                    for(int cc = c_start[ib_ins1];cc<=c_end[ib_ins1];cc++){
                                        onums[ob+blk_track*(seg_size)+cc] +=1;
                                        onums[ob+ol_track*(seg_size)+cc] -=1;
                                    }
                                }
                                else if(x[ib_ins1]==blk_track){
                                    x[ib_ins1] = ol_track;
                                    derta_wl+=(wlcost1[costb_ins1_trs_olt]
                                               -wlcost1[costb_ins1_trs_blkt]);
                                    for(int cc = c_start[ib_ins1];cc<=c_end[ib_ins1];cc++){
                                        onums[ob+blk_track*(seg_size)+cc] -=1;
                                        onums[ob+ol_track*(seg_size)+cc] +=1;
                                    }
                                }
                            }
                        }
                    }
                    x[ib_ins] = final_track1;

                    int ol_tmp3 = 0;
                    for(int cc = c_start[ib_ins];cc<=c_end[ib_ins];cc++){
                        int ob_final_track_seg_size_cc = ob + final_track1 * (seg_size) + cc;
                        if(onums[ob_final_track_seg_size_cc]>=1){
                            ol_tmp3 += (coord[coordb + cc + 1] - coord[coordb + cc]);
                        }
                        onums[ob_final_track_seg_size_cc] +=1 ;
                    }
                    blk_tmp1 += min_blk;
                    wl_tmp1 += (wlcost1[costb_ins_tr_size + final_track1] + derta_wl);
                    ol_tmp1 += ol_tmp3;
                }


                for (int a = subpanel_ins_begin[panel_ins_begin + tid]; a <= sub_ins_end; a++) {
                    int ins = inss[ib + a];
                    int costb_ins_tr_size = costb + ins * tr_size;
                    int ib_ins = ib + ins;

                    int old_track = x[panel_ins_begin+ins];
                    int new_track = x[panel_ins_begin+ins];
                    int old_ov = 0;
                    for (int cc = c_start[ib_ins]; cc <= c_end[ib_ins]; cc++) {
                        if (onums[ob + old_track * (seg_size) + cc] > 1) {
                            old_ov += (coord[coordb + cc + 1] - coord[coordb + cc]);
                        }
                    }
                    int tttmp_ov = old_ov;
                    for (int tt = 0; tt < tr_size; tt++) {
                        int costb_ins_trs_tt = costb_ins_tr_size + tt;
                        if (tt == old_track) continue;
                        if (blkcost1[costb + ins * tr_size + tt] ==
                            blkcost1[costb + ins * tr_size + new_track]) {
                            int new_ov = 0;
                            for (int cc = c_start[ib_ins]; cc <= c_end[ib_ins]; cc++) {
                                if (onums[ob + tt * (seg_size) + cc] >= 1) {
                                    new_ov += (coord[coordb + cc + 1] - coord[coordb + cc]);
                                }
                            }
                            if (tttmp_ov > new_ov) {
                                tttmp_ov = new_ov;
                                new_track = tt;
                            } else if (new_ov == tttmp_ov && wlcost1[costb_ins_trs_tt] <
                                                             wlcost1[costb_ins_tr_size +
                                                                    new_track]) {
                                new_track = tt;
                            }
                        }
                    }
                    if (new_track != old_track) {
                        x[panel_ins_begin + ins] = new_track;
                        for (int cc = c_start[ib + ins]; cc <= c_end[ib + ins]; cc++) {
                            onums[ob + old_track * (seg_size) + cc] -= 1;
                            onums[ob + new_track * (seg_size) + cc] += 1;
                        }
                        wl_tmp1 += (wlcost1[costb_ins_tr_size+ new_track]
                                   - wlcost1[costb_ins_tr_size+ old_track]);
                        if (old_ov != tttmp_ov) {
                            ol_tmp1 += (tttmp_ov - old_ov);
                        }
                    }
                }
                mtx.lock(); // 上锁
                blk_tmp += blk_tmp1;
                ol_tmp += ol_tmp1;
                wl_tmp += wl_tmp1;
                mtx.unlock(); // 解锁
            }




        }




//        cu_method2(x,tracks_size,interval_size,segments_size,
//                   coord,inss,c_start,c_end,
//                   coord_begin,ins_begin,cost_begin,
//                   blkcost1,wlcost1,
//                   coord_size,ins_size,cost_size,taDB->trackLayer[i].panels.size(),
//                   onums_size,onums_begin
//                ,blk_tmp,wl_tmp,ol_tmp,olcost,inss1,cost_To_Panel
//        );



        total_blkcost+= blk_tmp/10;
        total_olpcost+= ol_tmp/10;
        total_wlcost += wl_tmp/10;
//        total_blkcost+= blk_tmp;
//        total_olpcost+= ol_tmp;
//        total_wlcost += wl_tmp;



//        cost11 += ol_tmp/10;
//        cost22 += blk_tmp/10;


        int cost111 = 0;
        int cost222 =0;
        int cost333 = 0;



        for(int l=0;l<taDB->trackLayer[i].panels.size();++l)//panel,
        {
            Panel *panel = &taDB->trackLayer[i].panels[mapValue[l]];
            if(panel->intervalList.empty()) continue;
            int *xx = new int[panel->intervalList.size()];
            memset(xx,-1,sizeof(int)*panel->intervalList.size());
            for (int in = 0; in < interval_size[l]; in++) {
                xx[in] = x[ins_begin[l] + in];
//                xx[in] = 0;
                if(xx[in]<0){
                    cout<<"error"<<endl;
                }
            }
            AssignIrouteToTrack_Cu(panel, xx);

            vector<vector<int>> onumsss(panel->tracks.size(),vector<int>(segments_size[l],0));
            int ol_sum = 0;
            for(int in=0;in<interval_size[l];in++){
                cost222 += blkcost1[cost_begin[l]+in*tracks_size[l]+xx[in]];
                cost333 += wlcost1[cost_begin[l]+in*tracks_size[l]+xx[in]];

                for(int left = c_start[ins_begin[l]+in];left <= c_end[ins_begin[l]+in];left++){
                    if(onumsss[xx[in]][left]>=1){
                        ol_sum+= (coord[coord_begin[l]+left+1]-coord[coord_begin[l]+left]);
                    }
                    onumsss[xx[in]][left] += 1;
                }
            }
            cost111+=ol_sum;
        }

        cost11 +=cost111/10;
        cost22 +=cost222/10;
        cost33 +=cost333/10;



        delete[] tracks_size;
        delete[] interval_size;
        delete[] segments_size;

        for (int j = 0; j < taDB->trackLayer[i].panels.size(); ++j) {
            host_coord[j].clear();

//            host_blkcost[j].clear();
//            host_wlcost[j].clear();
        }
        auto end_time1 = std::chrono::high_resolution_clock::now();
        auto duration1 = std::chrono::duration_cast<std::chrono::milliseconds>(end_time1 - s_time1);
        total_duration_assign += duration1;

    }
    auto total_duration_update = std::chrono::milliseconds::zero();
    auto s_time12 = std::chrono::high_resolution_clock::now();



//Update netList
    for (int i=0;i<taDB->irouteList.size();++i)
    {
//local net
        if (taDB->irouteList[i].subNetIndex == -1)
            taDB->netList[taDB->irouteList[i].netIndex].trunk = taDB->irouteList[i];
        else//global net
            taDB->netList[taDB->irouteList[i].netIndex].subNetList[taDB->irouteList[i].subNetIndex] = taDB->irouteList[i];
    }
    auto end_time12 = std::chrono::high_resolution_clock::now();
    auto duration12 = std::chrono::duration_cast<std::chrono::milliseconds>(end_time12 - s_time12);
    total_duration_update += duration12;

    printf("The num of cut panel is %d\n",cutpanel);
    printf("panel add intervalList RunTime1: %d ms\n",runtime1.count());
    printf(" segment  Runtime: %d ms\n",total_duration_segment.count());
    printf(" cost  Runtime: %d ms\n",total_duration_cost.count());
    printf("Assign Runtime: %d ms\n",total_duration_assign.count());
    printf("update Runtime: %d ms\n",total_duration_update.count());

    add_ins = runtime1.count();
    segment_time = total_duration_segment.count();
    cost_time = total_duration_cost.count();
    assign_time = total_duration_assign.count();
    update_time = total_duration_update.count();
}
void ILPAssign::DPMethod3(){
    auto start_time = std::chrono::high_resolution_clock::now();

    int cutpanel=0;
    auto& irouteList = taDB->irouteList;
    auto& netList = taDB->netList;


    for (const auto & iroute : irouteList)
    {
        // 使用引用减少重复访问
        int subNetIndex = iroute.subNetIndex;
        int netIndex = iroute.netIndex;

        if (subNetIndex == -1) {
            netList[netIndex].trunk = iroute;
        } else {
            netList[netIndex].subNetList[subNetIndex] = iroute;
        }
    }





    auto& trackLayer = taDB->trackLayer;  // 使用引用来减少多次访问
    const auto& tileWidth = taDB->info.tileWidth;   // 提取常量
    const auto& tileHeight = taDB->info.tileHeight; // 提取常量
    Interval tempInterval;
    for (int i=0;i<irouteList.size();++i)
    {
        const auto& iroute = irouteList[i];  // 将常用变量存储到局部变量中
        int layer = iroute.source.layers;
        int netIndex = iroute.netIndex;

        auto& currentTrackLayer = trackLayer[layer];
        auto& panels = currentTrackLayer.panels;

        int panelCoord = currentTrackLayer.isVertical
                         ? GetBinCoord(iroute.source.x, tileWidth)
                         : GetBinCoord(iroute.source.y, tileHeight);

        tempInterval.irouteIndex = i;
        tempInterval.iroutesPinsCount = netList[netIndex].subNetList.size() + netList[netIndex].pinList.size();
        tempInterval.start = iroute.start;
        tempInterval.end = iroute.end;
        panels[panelCoord].intervalList.push_back(tempInterval);
    }
    printf("number of iroute %d\n",irouteList.size());


    auto end_time1 = std::chrono::high_resolution_clock::now();
    auto runtime1 = std::chrono::duration_cast<std::chrono::milliseconds>(end_time1-start_time);


    auto total_duration_segment = std::chrono::milliseconds::zero();
    auto total_duration_cost = std::chrono::milliseconds::zero();
    auto total_duration_assign = std::chrono::milliseconds::zero();


    std::vector<int> mapValue;
    int count;

    omp_set_num_threads(40);

    for(int i=2;i<trackLayer.size();++i)//Layer
    {
        auto start_time22 = std::chrono::high_resolution_clock::now();
        const auto& panels = trackLayer[i].panels;
        mapValue.resize(panels.size());
        count = 0;
        for (const auto& panel :  trackLayer[i].panels)
        {
            mapValue[count] = panel.first; // panelCoord
            ++count; // panel
        }
        int panel_size = panels.size();
        int *tracks_size = new int[panel_size];
        int *interval_size = new int[panel_size];
        int *segments_size = new int[panel_size];



        int *coord_begin = new int [panel_size];

        int *ins_begin = new int[panel_size];

        int *cost_begin = new int[panel_size];

        int *onums_begin = new int[panel_size];

        int  *track_begin = new int[panel_size];


        vector<vector<int>> host_coord(panel_size,vector<int>());


        cout << " layer:" << i << endl;
        int ins_size = 0;
        int all_track_size = 0;
        for(int l=0;l<panel_size;++l){
            Panel *panel = &trackLayer[i].panels[mapValue[l]];
            const auto& intervalList = panel->intervalList;
            const auto& tracks = panel->tracks;
            ins_begin[l] = ins_size;
            track_begin[l] = all_track_size;
            ins_size += intervalList.size();
            all_track_size += tracks.size();
            interval_size[l] = intervalList.size();
            tracks_size[l] = tracks.size();
        }

        int *inss = new int[ins_size];
        int *inss1 = new int[ins_size];
        int *c_start = new int[ins_size];
        int *c_end = new int[ins_size];
        #pragma omp parallel for schedule(dynamic,1)
        for (int l = 0; l < panel_size; ++l) {
            Panel *panel = &trackLayer[i].panels[mapValue[l]];
            const auto &intervalList = panel->intervalList;
            int i_begin = ins_begin[l];
            for (int in = 0; in < intervalList.size(); in++) {
                int a = i_begin + in;
                inss[a] = in;
                inss1[a] = in;
            }
            std::stable_sort(inss + i_begin, inss + i_begin + intervalList.size(), [&](int a, int b) {
                return intervalList[a].start < intervalList[b].start ||
                (intervalList[a].start == intervalList[b].start &&
                intervalList[a].end < intervalList[b].end);
            });

            std::stable_sort(inss1 + i_begin, inss1 + i_begin + intervalList.size(), [&](int a, int b) {
                return intervalList[a].end < intervalList[b].end ||
                (intervalList[a].end == intervalList[b].end &&
                intervalList[a].start < intervalList[b].start);
            });
        }
        #pragma omp parallel for schedule(dynamic,1)
        for (int l = 0; l < panel_size; ++l)
        {
            Panel *panel = &trackLayer[i].panels[mapValue[l]];

            const auto &intervalList = panel->intervalList;
            std::vector<int> coord(intervalList.size() * 2);
            int cnt = 0;
            for (const auto &interval: intervalList) {
                coord[cnt++] = interval.start;
                coord[cnt++] = interval.end;
            }

            std::set<int> unique_coords(coord.begin(), coord.end());
            segments_size[l] = unique_coords.size();
            std::unordered_map<int, int> coord_index;
            int index = 0;
            for (const int &val: unique_coords) {
                coord_index[val] = index++;
            }
            host_coord[l].assign(unique_coords.begin(), unique_coords.end());
            int ib = ins_begin[l];
            for (int ins = 0; ins < intervalList.size(); ++ins) {
                int start = coord_index[intervalList[ins].start];
                int end = coord_index[intervalList[ins].end] - 1;
                int ib_in = ib + ins;
                c_start[ib_in] = start;
                c_end[ib_in] = end;
            }
        }

        auto start_cost_time = std::chrono::high_resolution_clock::now();
        auto duration11 = std::chrono::duration_cast<std::chrono::milliseconds>(start_cost_time-start_time22);
        total_duration_segment += duration11;


        int layer_size = trackLayer.size();
        int *layer_Vertical = new int[layer_size];
        for(int l=0;l<layer_size;l++){
            layer_Vertical[l] = trackLayer[l].isVertical;
        }


        int *track_coord = new int [all_track_size];
        int *track_blk_begin = new int[all_track_size];
        int *track_blk_size = new int[all_track_size];
//        std::fill(track_blk_begin, track_blk_begin + all_track_size, 0);
//        std::fill(track_blk_size, track_blk_size + all_track_size, 0);
        int all_blk_size = 0;
        int cost_size = 0;

        int prev_coord = 0;
        int prev_onums = 0;
        int prev_track_begin = 0;
        int pre_ins = 0;
        int prev_track_blk_begin = 0;
        for(int l=0;l<panel_size;l++){
            Panel& panel = trackLayer[i].panels[mapValue[l]];
            cost_begin[l] = cost_size;

            cost_size += interval_size[l]*tracks_size[l];
            coord_begin[l] = prev_coord;
            onums_begin[l] = prev_onums;
            track_begin[l] = prev_track_begin;
            ins_begin[l] = pre_ins;

            prev_coord += segments_size[l];
            pre_ins += interval_size[l];
            prev_onums += segments_size[l]*tracks_size[l];
            prev_track_begin += tracks_size[l];

            int track_beginl = track_begin[l];
            for(int t=0;t<panel.tracks.size();t++){
                track_blk_begin[track_beginl+t] = prev_track_blk_begin;
                int tb_tmp = panel.tracks[t].blk.size();
                prev_track_blk_begin += panel.tracks[t].blk.size();
                const auto& blk = panel.tracks[t].blk;
                all_blk_size += tb_tmp;
                track_blk_size[track_beginl+t] = tb_tmp;
            }
        }

        int last_panel_index = trackLayer[i].panels.size()-1;
        int coord_size = coord_begin[last_panel_index]+host_coord[last_panel_index].size();
        int *coord = new int [coord_size];
        int onums_size = onums_begin[last_panel_index]+(segments_size[last_panel_index])*tracks_size[last_panel_index];
        int *cost_To_Panel = new int[cost_size];

        int *blk_start = new int [all_blk_size];
        int *blk_end = new int[all_blk_size];
        int *interval_start = new int[ins_size];
        int *interval_end = new int [ins_size];

        #pragma omp parallel for schedule(dynamic,1)
        for(int l=0;l<panel_size;l++) {
            Panel& panel = trackLayer[i].panels[mapValue[l]];
            int a = track_begin[l];
            const auto& tracks = panel.tracks;
            const auto& intervalList = panel.intervalList;

            int ins_b = ins_begin[l];
            auto track_size = tracks_size[l];
            int costb = cost_begin[l];
            for (int t = 0; t < track_size; t++) {
                int b = track_blk_begin[a+t];
                const auto &blks = tracks[t].blk;
                for (int tb = 0; tb < blks.size(); tb++) {
                    blk_start[b+tb] = blks[tb].start;
                    blk_end[b+tb] = blks[tb].end;
                }
            }

            for(int in=0;in<intervalList.size();in++){
                const auto& inte = intervalList[in];
                interval_start[ins_b+in] = inte.start;
                interval_end[ins_b+in] = inte.end;
                auto cp_begin = costb+in*track_size;
                for(int t =0;t<track_size;t++){
                    auto cpt = cp_begin+t;
                    cost_To_Panel[cpt] = l;
                }
            }
            int coob = coord_begin[l];
            int ib = ins_begin[l];
            for(int s=0;s<segments_size[l];s++){
                coord[coob+s] = host_coord[l][s];
            }
        }
        int* wlcost1 = new int[cost_size];
        int* blkcost1 = new int[cost_size];




        int *x = new int[ins_size];
        int *olcost;
        std::fill(x,x+ins_size,-1);



        printf("cost_Size %d\n",cost_size);



        calculate_blkcost(blkcost1,blk_start,blk_end,interval_start,interval_end,
                          track_blk_begin,track_blk_size,tracks_size,interval_size,
                          panel_size,cost_begin,cost_size,all_blk_size,ins_size,all_track_size,cost_To_Panel,
                          ins_begin,track_begin);


        unordered_set<int> unique_netIndex;



        for(int l=0;l<panel_size;l++) {
            auto &panel = trackLayer[i].panels[mapValue[l]];
            const auto& intervalList = panel.intervalList;
            for(auto& in : intervalList){
                int irouteIndex = in.irouteIndex;
                int netIndex = irouteList[irouteIndex].netIndex;
                unique_netIndex.insert(netIndex);
            }
        }


        vector<int> uni_vec(unique_netIndex.begin(),unique_netIndex.end());
        int ns = uni_vec.size();

        printf("ns %d\n",ns);
        int *subNettt_begin = new int[ns];
        int *subNet_size = new int[ns];
        int subNettt_size_sum = 0;
        int *pin_size = new int[ns];
        int *pin_begin = new int[ns];
        int pin_size_sum = 0;

        #pragma omp parallel for
        for (int unii = 0; unii < ns; ++unii) {
            const auto &net = netList[uni_vec[unii]];
            subNet_size[unii] = net.subNetList.size();
            pin_size[unii] = net.pinList.size();
        }

        // 计算 subNettt_begin 和 pin_begin 的增量
        subNettt_begin[0] = 0;
        pin_begin[0] = 0;
        for (int unii = 1; unii < ns; ++unii) {
            subNettt_begin[unii] = subNettt_begin[unii - 1] + subNet_size[unii - 1];
            pin_begin[unii] = pin_begin[unii - 1] + pin_size[unii - 1];
        }
        subNettt_size_sum = subNettt_begin[ns - 1] + subNet_size[ns - 1];
        pin_size_sum = pin_begin[ns - 1] + pin_size[ns - 1];


        Coordinate3D1 *pin_coord = new Coordinate3D1[pin_size_sum];
        Iroutttt *subNettt = new Iroutttt[subNettt_size_sum];


        #pragma omp parallel for schedule(dynamic,1)
        for (int unii = 0; unii < ns; ++unii) {
            const auto& net = netList[uni_vec[unii]];
            int net_begin = subNettt_begin[unii];
            int pb = pin_begin[unii];
            auto* local_subNettt = subNettt + net_begin;
            auto* local_pin_coord = pin_coord + pb;
            for (int si = 0; si < subNet_size[unii]; ++si) {
                const auto& subNet = net.subNetList[si];
                local_subNettt[si].source.x = subNet.source.x;
                local_subNettt[si].source.y = subNet.source.y;
                local_subNettt[si].source.layers = subNet.source.layers;
                local_subNettt[si].target.x = subNet.target.x;
                local_subNettt[si].target.y = subNet.target.y;
                local_subNettt[si].target.layers = subNet.target.layers;
                local_subNettt[si].start = subNet.start;
                local_subNettt[si].end = subNet.end;
                local_subNettt[si].assigned = subNet.assigned;
            }
            for (int pi = 0; pi < net.pinList.size(); ++pi) {
                const auto& pin = net.pinList[pi];
                local_pin_coord[pi].x = pin.x;
                local_pin_coord[pi].y = pin.y;
                local_pin_coord[pi].layers = pin.layers;
            }
        }

        unordered_map<int, int> index_map;
        index_map.reserve(uni_vec.size());  // 预分配空间，避免动态扩展
        for(int unii = 0; unii < uni_vec.size(); unii++) {
            index_map[uni_vec[unii]] = unii;
        }
        printf("subNettt_size_sum%d  \nns  %d\n",subNettt_size_sum,ns);

        int *net_Index = new int[ins_size];
        int *subnet_Index = new int[ins_size];
        Coordinate3D1 *source = new Coordinate3D1[ins_size];

        #pragma omp parallel for schedule(dynamic,1)
        for(int l=0;l<panel_size;l++){
            Panel *panel = &trackLayer[i].panels[mapValue[l]];
            const auto& intervalList = panel->intervalList;
            const auto& tracks = panel->tracks;
            for(int k=0;k<panel->tracks.size();k++){
                track_coord[track_begin[l]+k]  = tracks[k].coordinate;
            }
            int count = 0;
            for (const auto& interval : intervalList) {
                int irouteIndex = interval.irouteIndex;
                int netIndex = irouteList[irouteIndex].netIndex;
                int subNetIndex = irouteList[irouteIndex].subNetIndex;
                int ins_begin_in = ins_begin[l] + count;
                count ++;
                if (index_map.find(netIndex) != index_map.end()) {
                    net_Index[ins_begin_in] = index_map[netIndex];
                } else {
                    printf("error not found\n");
                }
                subnet_Index[ins_begin_in] = subNetIndex;
                const auto& trunk_source = netList[netIndex].trunk.source;
                if (subNetIndex == -1) {
                    source[ins_begin_in].x = trunk_source.x;
                    source[ins_begin_in].y = trunk_source.y;
                    source[ins_begin_in].layers = trunk_source.layers;
                } else {

                }
            }
        }

        calculate_cost1(wlcost1, net_Index, source, layer_Vertical, track_coord
                , subNet_size, subNettt_begin, pin_size, pin_begin, pin_size_sum
                , pin_coord
                , ins_size, layer_size, cost_begin, cost_size, all_track_size
                , interval_size, tracks_size, panel_size, cost_To_Panel, ins_begin, track_begin, subnet_Index
                , subNettt, ns, subNettt_size_sum
                );

        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_cost_time);
        total_duration_cost += duration;


        auto s_time1 = std::chrono::high_resolution_clock::now();
        int wl_tmp = 0;
        int blk_tmp = 0;
        int ol_tmp = 0;
        cu_method2(x,tracks_size,interval_size,segments_size,
                   coord,inss,c_start,c_end,
                   coord_begin,ins_begin,cost_begin,
                   blkcost1,wlcost1,
                   coord_size,ins_size,cost_size,panel_size,
                   onums_size,onums_begin
                ,blk_tmp,wl_tmp,ol_tmp,olcost,inss1,cost_To_Panel
        );

        total_blkcost+= blk_tmp/10;
        total_olpcost+= ol_tmp/10;
        total_wlcost += wl_tmp/10;

        int cost111 = 0;
        int cost222 =0;
        int cost333 = 0;


        for(int l=0;l<panel_size;++l)//panel,
        {
            Panel *panel = &trackLayer[i].panels[mapValue[l]];
            const auto& intervalList = panel->intervalList;
            if(intervalList.empty()) continue;
            int *xx = new int[intervalList.size()];
            memset(xx,-1,sizeof(int)*intervalList.size());
            for (int in = 0; in < interval_size[l]; in++) {
//                xx[in] = x[ins_begin[l] + in];
                xx[in] = 0;
//                if(x[ins_begin[l]+in]<0){
//                    cout<<"error"<<endl;
//                }
            }
            AssignIrouteToTrack_Cu(panel, xx);

            vector<vector<int>> onumsss(panel->tracks.size(),vector<int>(segments_size[l],0));
            int ol_sum = 0;
            for(int in=0;in<interval_size[l];in++){
                cost222 += blkcost1[cost_begin[l]+in*tracks_size[l]+xx[in]];
                cost333 += wlcost1[cost_begin[l]+in*tracks_size[l]+xx[in]];

                for(int left = c_start[ins_begin[l]+in];left <= c_end[ins_begin[l]+in];left++){
                    if(onumsss[xx[in]][left]>=1){
                        ol_sum+= (coord[coord_begin[l]+left+1]-coord[coord_begin[l]+left]);
                    }
                    onumsss[xx[in]][left] += 1;
                }
            }
            cost111+=ol_sum;
        }
        cost11 +=cost111/10;
        cost22 +=cost222/10;
        cost33 +=cost333/10;



        delete[] tracks_size;
        delete[] interval_size;
        delete[] segments_size;

        for (int j = 0; j < panel_size; ++j) {
            host_coord[j].clear();
        }
        auto end_time1 = std::chrono::high_resolution_clock::now();
        auto duration1 = std::chrono::duration_cast<std::chrono::milliseconds>(end_time1 - s_time1);
        total_duration_assign += duration1;

    }
    auto total_duration_update = std::chrono::milliseconds::zero();
    auto s_time12 = std::chrono::high_resolution_clock::now();



//Update netList
    for (int i=0;i<irouteList.size();++i)
    {
//local net
        if (irouteList[i].subNetIndex == -1)
            netList[irouteList[i].netIndex].trunk = irouteList[i];
        else//global net
            netList[irouteList[i].netIndex].subNetList[irouteList[i].subNetIndex] = irouteList[i];
    }
    auto end_time12 = std::chrono::high_resolution_clock::now();
    auto duration12 = std::chrono::duration_cast<std::chrono::milliseconds>(end_time12 - s_time12);
    total_duration_update += duration12;

    printf("The num of cut panel is %d\n",cutpanel);
    printf("panel add intervalList RunTime1: %d ms\n",runtime1.count());
    printf(" segment  Runtime: %d ms\n",total_duration_segment.count());
    printf(" cost  Runtime: %d ms\n",total_duration_cost.count());
    printf("Assign Runtime: %d ms\n",total_duration_assign.count());
    printf("update Runtime: %d ms\n",total_duration_update.count());
}


void ILPAssign::DPMethod2(){
    auto start_time = std::chrono::high_resolution_clock::now();

    int cutpanel=0;
//update
    for (int i=0;i<taDB->irouteList.size();++i)
    {
        if (taDB->irouteList[i].subNetIndex == -1)
            taDB->netList[taDB->irouteList[i].netIndex].trunk = taDB->irouteList[i];
        else
            taDB->netList[taDB->irouteList[i].netIndex].subNetList[taDB->irouteList[i].subNetIndex]
            = taDB->irouteList[i];
    }

    int layer;
    int panelCoord;
    Interval tempInterval;

    for (int i=0;i<taDB->irouteList.size();++i)
    {
        layer = taDB->irouteList[i].source.layers;
//get the coord of the panel
        if (taDB->trackLayer[layer].isVertical)//vertical
        {
            panelCoord =
                    GetBinCoord(taDB->irouteList[i].source.x,taDB->info.tileWidth);
        }
        else//horizontal
        {
            panelCoord =
                    GetBinCoord(taDB->irouteList[i].source.y,taDB->info.tileHeight);
        }
        tempInterval.irouteIndex = i;
        tempInterval.iroutesPinsCount =
                taDB->netList[taDB->irouteList[i].netIndex].subNetList.size()
                +taDB->netList[taDB->irouteList[i].netIndex].pinList.size();//?
        tempInterval.start = taDB->irouteList[i].start;
        tempInterval.end = taDB->irouteList[i].end;
        taDB->trackLayer[layer].panels[panelCoord].intervalList.push_back(tempInterval);
    }


    printf("number of iroute %d\n",taDB->irouteList.size());





    omp_lock_t mylock;
    omp_init_lock(&mylock);


    int *mapValue,count;

    for(int i=2;i<taDB->trackLayer.size();++i)//Layer
    {
        count = 0;
        mapValue = new int[taDB->trackLayer[i].panels.size()];
        for (map<int,Panel>::iterator it = taDB->trackLayer[i].panels.begin();it!=taDB->trackLayer[i].panels.end();++it)
        {
            mapValue[count] = it->first;//panelCoord
            ++count;//panel
        }
        #pragma omp parallel for schedule(dynamic,1)
        for(int l=0;l<taDB->trackLayer[i].panels.size();++l)//panel,
        {

            Panel *panel = &taDB->trackLayer[i].panels[mapValue[l]];
            Interval interval;
            if(panel->intervalList.empty()) continue;
            int *coord, cnt = 0;

            coord = new int[panel->intervalList.size() * 2];

//dividing
            for (int in = 0; in < panel->intervalList.size(); in++) {
                coord[cnt++] = panel->intervalList[in].start;
                coord[cnt++] = panel->intervalList[in].end;
            }

            sort(coord, coord + panel->intervalList.size() * 2);//sort from small to large


//deduplication
            int numofsegments = unique(coord, coord + panel->intervalList.size() * 2) - coord;
//            printf("num of segments:%d\n", numofsegments - 1);
//            cout << " layer:" << i << " panel:" << l << endl;
//            cout << "iroutes of panel:" << panel->intervalList.size() << endl;



            int *c = new int[panel->intervalList.size()*(numofsegments-1)];
            memset(c, 0, ((numofsegments - 1)*panel->intervalList.size()) * sizeof(int));


            for(int in = 0; in < panel->intervalList.size(); ++in) {
                for (int k = 0; k < numofsegments - 1; ++k) {
                    if (panel->intervalList[in].start == coord[k]) {
                        while (coord[k] != panel->intervalList[in].end){
                            c[in*(numofsegments-1)+k] = 1;
                            k++;
                        }
                        break;
                    }
                }
            }

            int *intervalList_index = new int[panel->intervalList.size()];
            vector<int> subpanel_begin;

            for(int in=0;in<panel->intervalList.size();in++){
                intervalList_index[in] = in;
            }
            sort(intervalList_index,intervalList_index+panel->intervalList.size(),[&](int a,int b){
                return panel->intervalList[a].start<panel->intervalList[b].start;
            });


            int *c_start = new int[panel->intervalList.size()];
            int *c_end = new int[panel->intervalList.size()];
            for(int ins = 0;ins<panel->intervalList.size();ins++){
                int start = -1;
                int end = -1;
                for(int s=0;s<numofsegments-1;s++){
                    if(c[ins*(numofsegments-1)+s]==1&&start == -1){
                        start = s;
                    }
                    else if(c[ins*(numofsegments-1)+s]==0&&start != -1){
                        end = s-1;
                        break;
                    }
                }
                if(end==-1&&start != -1){
                    end = numofsegments-1-1;
                }
                c_start[ins]= start;
                c_end[ins] = end;
            }

            int sub_begin = 0;
            int most_right = panel->intervalList[intervalList_index[0]].end;
            subpanel_begin.push_back(sub_begin);
            for(int index=0;index<panel->intervalList.size();index++){
                int in = intervalList_index[index];
                if(panel->intervalList[in].start>most_right){
                    int start = subpanel_begin[subpanel_begin.size()-1];
                    subpanel_begin.push_back(index);
                    most_right = panel->intervalList[in].end;
                }
                else if(panel->intervalList[in].start<=most_right&&panel->intervalList[in].end>most_right){
                    most_right = panel->intervalList[in].end;
                }
            }



            OverlapResult overlapresult, blkresult, result;
            result.overlapCount = 0;
            result.overlapLength = 0;
            result.overlapBlk = 0;
            double *blkcost = new double[panel->intervalList.size()*panel->tracks.size()];
            double *wlcost = new double[panel->intervalList.size()*panel->tracks.size()];
            std::fill(wlcost, wlcost + panel->intervalList.size() * panel->tracks.size(), 0.0f);

            for(int in=0;in<panel->intervalList.size();in++){
                int irouteIndex, netIndex, subNetIndex;
                vector<double> tracksCost;
                int minCost = 0;
                auto interval = panel->intervalList[in];
                for(int t=0;t<panel->tracks.size();t++){
                    blkcost[in*panel->tracks.size()+t] = GetBlkResult(result, panel->tracks[t], panel->intervalList[in],false).overlapBlk;
                }
                irouteIndex = interval.irouteIndex;
                netIndex = taDB->irouteList[irouteIndex].netIndex;
                subNetIndex = taDB->irouteList[irouteIndex].subNetIndex;
                int layer;
                int panelCoord;
                if (subNetIndex == -1) {
                    layer = taDB->netList[netIndex].trunk.source.layers;
                    if (taDB->trackLayer[layer].isVertical) {
                        panelCoord = GetBinCoord(taDB->netList[netIndex].trunk.source.x, taDB->info.tileWidth);
                        for (int k = 0; k < taDB->trackLayer[layer].panels[panelCoord].tracks.size(); ++k) {
                            wlcost[in*panel->tracks.size()+k] += abs(taDB->netList[netIndex].trunk.source.x -
                                                 taDB->trackLayer[layer].panels[panelCoord].tracks[k].coordinate);
                        }
                    }
                    else {
                        panelCoord = GetBinCoord(taDB->netList[netIndex].trunk.source.y, taDB->info.tileHeight);
                        for (int k = 0; k < taDB->trackLayer[layer].panels[panelCoord].tracks.size(); ++k)
                            wlcost[in*panel->tracks.size()+k] += abs(taDB->netList[netIndex].trunk.source.y -
                                                 taDB->trackLayer[layer].panels[panelCoord].tracks[k].coordinate);
                    }
                    continue;
                }
                layer = taDB->netList[netIndex].subNetList[subNetIndex].source.layers;
                Iroute irouteOnTrack = taDB->netList[netIndex].subNetList[subNetIndex];
                Coordinate3D pin;

                if (taDB->trackLayer[layer].isVertical) {
                    panelCoord = GetBinCoord(taDB->netList[netIndex].subNetList[subNetIndex].source.x,
                                             taDB->info.tileWidth);
                    int minCost = 1e9;
                    for (int t = 0; t < taDB->trackLayer[layer].panels[panelCoord].tracks.size(); ++t) {
                        irouteOnTrack.source.x = irouteOnTrack.target.x = taDB->trackLayer[layer].panels[panelCoord].tracks[t].coordinate;
                        for (int n = 0; n < taDB->netList[netIndex].subNetList.size(); ++n) {
                            if (!taDB->netList[netIndex].subNetList[n].assigned)
                                continue;

                            if (taDB->netList[netIndex].subNetList[subNetIndex].source.layers ==
                                taDB->netList[netIndex].subNetList[n].source.layers)
                                continue;
                            minCost = min(minCost,
                                          IroutesDistance(irouteOnTrack, taDB->netList[netIndex].subNetList[n]));
                        }
                        for (int n = 0; n < taDB->netList[netIndex].pinList.size(); ++n) {
                            pin = taDB->netList[netIndex].pinList[n];
                            pin.x *= 10;
                            pin.y *= 10;

                            minCost = min(minCost, IroutePinDistance(irouteOnTrack, pin));
                        }
                        if (minCost == 1e9)
                            minCost = 0;

                        wlcost[in*panel->tracks.size()+t] += minCost;
                    }
                }
                else {
                    panelCoord = GetBinCoord(taDB->netList[netIndex].subNetList[subNetIndex].source.y,
                                             taDB->info.tileHeight);
                    int minCost = 1e9;
                    for (int t = 0; t < taDB->trackLayer[layer].panels[panelCoord].tracks.size(); ++t) {
                        irouteOnTrack.source.y = irouteOnTrack.target.y = taDB->trackLayer[layer].panels[panelCoord].tracks[t].coordinate;
                         for (int n = 0; n < taDB->netList[netIndex].subNetList.size(); ++n) {
                             if (!taDB->netList[netIndex].subNetList[n].assigned)
                                continue;
                             if (taDB->netList[netIndex].subNetList[subNetIndex].source.layers ==
                                taDB->netList[netIndex].subNetList[n].source.layers)
                                continue;
                            minCost = min(minCost,
                                          IroutesDistance(irouteOnTrack, taDB->netList[netIndex].subNetList[n]));
                        }
                        for (int n = 0; n < taDB->netList[netIndex].pinList.size(); ++n) {
                            pin = taDB->netList[netIndex].pinList[n];
                            pin.x *= 10;
                            pin.y *= 10;
                            minCost = min(minCost, IroutePinDistance(irouteOnTrack, pin));
                        }
                        if (minCost == 1e9)
                            minCost = 0;
                        wlcost[in*panel->tracks.size()+t] += minCost;
                    }
                }
            }

            int *x = new int[panel->intervalList.size()];
            for(int in = 0;in<panel->intervalList.size();in++){
                x[in] = -1;
            }


            int *subpanel_begin1 = new int[subpanel_begin.size()];
            int subpanel_size = subpanel_begin.size();
            for(int sb=0;sb<subpanel_begin.size();sb++){
                subpanel_begin1[sb] = subpanel_begin[sb];
            }

            int *in_To_track=new int[panel->intervalList.size()*panel->tracks.size()];
            int *in_To_track_size = new int[panel->intervalList.size()];
            int *c_To_in = new int[(numofsegments-1)*panel->intervalList.size()];
            int *c_To_in_size= new int[(numofsegments-1)];
            std::fill(in_To_track_size, in_To_track_size + panel->intervalList.size(), 0);
            std::fill(c_To_in_size, c_To_in_size + numofsegments-1, 0);

            for(int in=0;in<panel->intervalList.size();in++) {
                double min_blk_in_t = DBL_MAX;
                int* to_Track = new int [panel->tracks.size()];
                int to_Track_size = 0;
                for (int t = 0; t < panel->tracks.size(); t++) {
                    int index = in * panel->tracks.size() + t;
                    if (blkcost[index] == min_blk_in_t) {
                        to_Track[to_Track_size] = t;
                        to_Track_size++;
                    } else if (blkcost[index] < min_blk_in_t) {
                        min_blk_in_t = blkcost[index];
                        to_Track_size = 0;
                        for (int a = 0; a < panel->tracks.size(); a++) {
                            to_Track[a] = -1;
                        }
                        to_Track[to_Track_size] = t;
                        to_Track_size++;
                    }
                }
                for (int t = 0; t < to_Track_size; t++) {
                    in_To_track[in*panel->tracks.size()+t] = to_Track[t];
                }
                for (int t = to_Track_size; t < panel->tracks.size(); t++) {
                    in_To_track[in*panel->tracks.size()+t] = -1;
                }
                in_To_track_size[in] = to_Track_size;
                delete[] to_Track;
            }

            for(int ins = 0;ins < panel->intervalList.size();ins++){
                for(int k=c_start[ins];k <= c_end[ins];k++){
                    c_To_in[k*panel->intervalList.size()+c_To_in_size[k]] = ins;
                    c_To_in_size[k] = c_To_in_size[k]+1;
                }
            }


            cu_method(
                    x,panel->tracks.size(),panel->intervalList.size(),numofsegments,wlcost,blkcost,
                    c,coord,intervalList_index,subpanel_size,subpanel_begin1,c_start,c_end
                    ,in_To_track,in_To_track_size,c_To_in,c_To_in_size);

//            if(panel->intervalList.size()<=100){
//                for(int in=0;in<panel->intervalList.size();in++){
//                    printf("%d ",x[in]);
//                    if(in%20==19) printf("\n");
//                }
//            }


            double wl_temp = 0;
            int  blk_temp = 0;
            int  ol_temp = 0;
            for (int in = 0; in < panel->intervalList.size(); in++) {
                int tr = x[in];
                wl_temp += wlcost[in*panel->tracks.size()+tr];
                blk_temp += blkcost[in*panel->tracks.size()+tr];
            }
            for (int tr = 0; tr < panel->tracks.size(); tr++) {
                vector<bool> overlap2(numofsegments-1, false);
                for (int irr = 0; irr < panel->intervalList.size(); irr++) {
                    if (x[irr] == tr) {
                        for (int s = c_start[irr]; s <= c_end[irr]; s++) {
//                                    for (int s = 0; s < subpanel->numofsegments; s++) {
                            if (c[irr*(numofsegments-1)+s] && !overlap2[s]) overlap2[s] = true;
                            else if (c[irr*(numofsegments-1)+s] && overlap2[s]) {
                                ol_temp += (coord[s + 1] - coord[s ]);
                            }
                        }
                    }
                }
            }
            omp_set_lock(&mylock);
            AssignIrouteToTrack_Cu(panel, x);
            total_blkcost+= blk_temp/10;
            total_olpcost+= ol_temp/10;
            total_wlcost += wl_temp/10;
            cost11 += ol_temp/10;
            cost22 += blk_temp/10;
            omp_unset_lock(&mylock);


            delete[] c;
            delete[] intervalList_index;
            delete[] c_start;
            delete[] c_end;
            delete[] blkcost;
            delete[] wlcost;

        }

    }


//Update netList
    for (int i=0;i<taDB->irouteList.size();++i)
    {
//local net
        if (taDB->irouteList[i].subNetIndex == -1)
            taDB->netList[taDB->irouteList[i].netIndex].trunk = taDB->irouteList[i];
        else//global net
            taDB->netList[taDB->irouteList[i].netIndex].subNetList[taDB->irouteList[i].subNetIndex] = taDB->irouteList[i];
    }


    printf("The num of cut panel is %d\n",cutpanel);


}



int ILPAssign::ConnectIrouteAndPinByGcell()
{
    double totalIrouteCost = 0;

    MST mst;//minimum spanning tree

//erase via and add pin to subNet
    int netNumb;
    int maxNetNumb = -1;
    Iroute ir;
    vector<Net> tempNetList = taDB->netList;
    std::mutex mtx;
    for (int i=0;i<tempNetList.size();++i)
    {
        for (vector<Iroute>::iterator it = tempNetList[i].subNetList.begin();it != tempNetList[i].subNetList.end();)
        {
            if (it->source.layers != it->target.layers)//erase via
                it = tempNetList[i].subNetList.erase(it);
            else
                ++it;
        }

        for (int j=0;j<tempNetList[i].pinList.size();++j)
        {
            ir.source = tempNetList[i].pinList[j];
            ir.source.x*=10;
            ir.source.y*=10;
            ir.target.x = -1; //define this iroute struct is pin
            tempNetList[i].subNetList.push_back(ir);
        }

//        netNumb = tempNetList[i].subNetList.size();
//        maxNetNumb = max(maxNetNumb,netNumb);
    }
    for(int i=0;i<tempNetList.size();++i){
        netNumb = tempNetList[i].subNetList.size();
        maxNetNumb = max(maxNetNumb,netNumb);
    }


    int** graph;
    graph = new int*[maxNetNumb];
    for(int i=0;i<maxNetNumb;++i)
        graph[i] = new int[maxNetNumb];

    map<int,vector<Iroute> > GcellIroute;
    Coordinate3D coord,coord2;
    int binNumb,binNumb2;
    for (int i=0;i<tempNetList.size();++i)
    {

        if (tempNetList[i].pinConnectCost != -1)//connect all pins to the trunk of local net
        {
            if (tempNetList[i].trunk.source.x == tempNetList[i].trunk.target.x)
                for (int j=0;j<tempNetList[i].pinList.size();++j)
                    totalIrouteCost += std::abs(tempNetList[i].trunk.source.x - tempNetList[i].pinList[j].x*10);
            else
                for (int j=0;j<tempNetList[i].pinList.size();++j)
                    totalIrouteCost += std::abs(tempNetList[i].trunk.source.y - tempNetList[i].pinList[j].y*10);

//totalIrouteCost += tempNetList[i].pinConnectCost;
            continue;
        }

//Create gcell map---------------------------------------
        GcellIroute.clear();
        for (int j=0;j<tempNetList[i].subNetList.size();++j)
        {
            if (tempNetList[i].subNetList[j].target.x == -1)//pin
            {
                coord = GetBinCoord_3D(tempNetList[i].subNetList[j].source);
                binNumb = (coord.y/(taDB->info.tileHeight*10))*taDB->info.grid.x;
                binNumb += coord.x/(taDB->info.tileWidth*10);

//create
                if (GcellIroute.find(binNumb) == GcellIroute.end())
                {
                    vector<Iroute> temp;
                    GcellIroute[binNumb] = temp;
                }

                GcellIroute[binNumb].push_back(tempNetList[i].subNetList[j]);
            }
            else//Iroute
            {
                coord = GetBinCoord_3D(tempNetList[i].subNetList[j].source);
                coord2 = GetBinCoord_3D(tempNetList[i].subNetList[j].target);

                binNumb = (coord.y/(taDB->info.tileHeight*10))*taDB->info.grid.x;
                binNumb += coord.x/(taDB->info.tileWidth*10);

                binNumb2 = (coord2.y/(taDB->info.tileHeight*10))*taDB->info.grid.x;
                binNumb2 += coord2.x/(taDB->info.tileWidth*10);

//horizontal
                if (!taDB->trackLayer[tempNetList[i].subNetList[j].source.layers].isVertical)
                {
                    for (int k=binNumb;k<=binNumb2;++k)
                    {
//create
                        if (GcellIroute.find(k) == GcellIroute.end())
                        {
                            vector<Iroute> temp;
                            GcellIroute[k] = temp;
                        }

                        GcellIroute[k].push_back(tempNetList[i].subNetList[j]);
                    }
                }
                else //vertical
                {
                    while(binNumb <= binNumb2)
                    {
//create
                        if (GcellIroute.find(binNumb) == GcellIroute.end())
                        {
                            vector<Iroute> temp;
                            GcellIroute[binNumb] = temp;
                        }
                        else
                        {
                            int aa = 0;
                        }

                        GcellIroute[binNumb].push_back(tempNetList[i].subNetList[j]);

                        binNumb += taDB->info.grid.x;
                    }

                    int aa = 0;
                }

            }
        }
//Create Map end----------------------------------------------------------------------------------


//Calculate each map pin/iroute connect cost
        for (map<int,vector<Iroute> >::iterator it=GcellIroute.begin();it!=GcellIroute.end();++it)
        {
            if (it->second.size()<2)
                continue;

            int temp;

            for (int j=0;j<it->second.size();++j)
            {
                graph[j][j] = 0;
                for (int k=j+1;k<it->second.size();++k)
                {
                    if (it->second[j].target.x != -1 && it->second[k].target.x != -1)
                    {
                        graph[j][k] = graph[k][j] = IroutesDistance(it->second[j],it->second[k]);
                    }
                    else if (it->second[j].target.x == -1 && it->second[k].target.x == -1)
                    {
                        temp=graph[j][k] = graph[k][j] = abs(it->second[j].source.x - it->second[k].source.x) + abs(it->second[j].source.y - it->second[k].source.y);
                    }
                    else
                    {
                        temp=graph[j][k] = graph[k][j] = IroutePinDistance(it->second[j],it->second[k].source);
                    }
                }
            }

            temp = taDB->netList[i].netConnectCost = mst.GetMSTcost(it->second.size(),graph);
            totalIrouteCost += taDB->netList[i].netConnectCost;
        }
    }

    printf("total Iroute connect cost %f\n",totalIrouteCost/10);
    totalIrouteCost1 = totalIrouteCost/10;
    tempNetList.clear();

    return totalIrouteCost/10;
}
int ILPAssign::ConnectIrouteAndPin()
{
    double delay = 0;
    double c0[8]={0.000101137,0.000102332,0.000095873,0.000115794,
                  0.000105372,0.0000986506,0.0000987576,0.000105018};
    double r0[8]={0.0271234,0.0271234,0.0271234,0.0055652,
                  0.0055652,0.0055652,0.000993014,0.000993014};
    double c_out = 0.001;//负载
    int r_driver = 1;//驱动电阻

    MST mst;//minimum spanning tree

//erase via and add pin to subNet
    int netNumb;
    int maxNetNumb = -1;
    Iroute ir;
    vector<Net> tempNetList = taDB->netList;
    for (unsigned int i=0;i<tempNetList.size();++i)
    {
//erase via
        for (vector<Iroute>::iterator it = tempNetList[i].subNetList.begin();it != tempNetList[i].subNetList.end();)
        {
            if (it->source.layers != it->target.layers)
                it = tempNetList[i].subNetList.erase(it);
            else
                ++it;
        }

        for (int j=0;j<tempNetList[i].pinList.size();++j)
        {
            ir.source = tempNetList[i].pinList[j];
            ir.source.x*=10;
            ir.source.y*=10;

            if(j==0) {
                ir.target.x = -1; //define this iroute struct is pin(source)
                tempNetList[i].subNetList.insert(tempNetList[i].subNetList.begin(),ir);
            }
            else {
                ir.target.x = -2;//define this iroute struct is pin(sink)
                tempNetList[i].subNetList.push_back(ir);
            }
        }

        netNumb = tempNetList[i].subNetList.size();
        maxNetNumb = max(maxNetNumb,netNumb);
    }
//printf("111\n");
//iroutes and Pin connect----------------------------------------------------------------------
    int temp;
    int** graph;
    graph = new int*[maxNetNumb];
    for(int i=0;i<maxNetNumb;++i)
        graph[i] = new int[maxNetNumb];

    double totalIrouteCost = 0;
    for (int i=0;i<tempNetList.size();++i)
    {

        if (tempNetList[i].pinConnectCost != -1)
        {
            if (tempNetList[i].trunk.source.x == tempNetList[i].trunk.target.x)
                for (int j=0;j<tempNetList[i].pinList.size();++j)
                    totalIrouteCost += std::abs(tempNetList[i].trunk.source.x - tempNetList[i].pinList[j].x*10);
            else
                for (int j=0;j<tempNetList[i].pinList.size();++j)
                    totalIrouteCost += std::abs(tempNetList[i].trunk.source.y - tempNetList[i].pinList[j].y*10);

            totalIrouteCost += tempNetList[i].pinConnectCost;
            continue;
        }
//printf("222\n");
        for (int j=0;j<tempNetList[i].subNetList.size();++j)
        {
            graph[j][j] = 0;
            for (int k=j+1;k<tempNetList[i].subNetList.size();++k)
            {
                if ((tempNetList[i].subNetList[j].target.x != -1 && tempNetList[i].subNetList[j].target.x != -2)  && (tempNetList[i].subNetList[k].target.x != -1 && tempNetList[i].subNetList[k].target.x != -2))
                {
                    graph[j][k] = graph[k][j] = IroutesDistance(tempNetList[i].subNetList[j],tempNetList[i].subNetList[k]);
                }
                else if ((tempNetList[i].subNetList[j].target.x == -1 || tempNetList[i].subNetList[j].target.x == -2) && (tempNetList[i].subNetList[k].target.x == -1 || tempNetList[i].subNetList[k].target.x == -2))
                {
                    temp=graph[j][k] = graph[k][j] = abs(tempNetList[i].subNetList[j].source.x - tempNetList[i].subNetList[k].source.x) + abs(tempNetList[i].subNetList[j].source.y - tempNetList[i].subNetList[k].source.y);
                }
                else
                {
                    temp=graph[j][k] = graph[k][j] = IroutePinDistance(tempNetList[i].subNetList[j],tempNetList[i].subNetList[k].source);
                }
            }
        }
//printf("333\n");
        temp = taDB->netList[i].netConnectCost = mst.GetMSTcost(tempNetList[i].subNetList.size(),graph);
        totalIrouteCost += taDB->netList[i].netConnectCost;
//printf("aaa\n");

//delay.
//        int* parent =  mst.GetMST(tempNetList[i].subNetList.size(),graph);
////获取栄1�7
//        int child=0;
//        int size = 0;//该线网引脚数
////printf("bbb\n");
//        vector<int> sink;//存储该线网内的sik下标
//        for (int j = 0; j < tempNetList[i].subNetList.size(); j++)
//        {
//            if(tempNetList[i].subNetList[j].target.x==-2)
//                sink.push_back(j);
//            if(parent[j]==0) child++;
//        }
//        size = sink.size()+1;//线网引脚敄1�7
////printf("引脚数：%d\n",size);
//// printf("组件数：%d\n",tempNetList[i].subNetList.size());
//        std::vector<double> len(tempNetList[i].subNetList.size(), 0.0);
//// for (int j=0;j<tempNetList[i].subNetList.size();++j){
////	printf(" %f ",len[j]);
//
//// }
//// printf("\n");
//        for (int j=0;j<tempNetList[i].subNetList.size();++j){
//
//            for (int k=1;k<tempNetList[i].subNetList.size();++k){
//
//                if(j==parent[k]) {
//                    break;
//                }
//            }
//
//
//            if(tempNetList[i].subNetList[j].target.x!=-2) {continue;}//叶子节点是iroutes跳过
//
//            int pos = j;
//
//
//            while(parent[pos]!=-1){
//                if ((tempNetList[i].subNetList[pos].target.x != -1 && tempNetList[i].subNetList[pos].target.x != -2)  && (tempNetList[i].subNetList[parent[pos]].target.x != -1 && tempNetList[i].subNetList[parent[pos]].target.x != -2))
//                {
//                    len[pos] = IroutesDistance(tempNetList[i].subNetList[pos],tempNetList[i].subNetList[parent[pos]]);
//                }
//                else if ((tempNetList[i].subNetList[pos].target.x == -1 || tempNetList[i].subNetList[pos].target.x == -2) && (tempNetList[i].subNetList[parent[pos]].target.x == -1 || tempNetList[i].subNetList[parent[pos]].target.x == -2))
//                {
//                    len[pos] =  abs(tempNetList[i].subNetList[pos].source.x - tempNetList[i].subNetList[parent[pos]].source.x) + abs(tempNetList[i].subNetList[pos].source.y - tempNetList[i].subNetList[parent[pos]].source.y);
//                }
//                else
//                {
//                    len[pos] = IroutePinDistance(tempNetList[i].subNetList[pos],tempNetList[i].subNetList[parent[pos]].source);
//                }
//                pos = parent[pos];
//            }
//
//
//        }
//
////printf("calculate len finish!\n");
//// for (int j=0;j<tempNetList[i].subNetList.size();++j){
//// printf("%f ",len[j]);
////}
//// printf("\n");
//
//        cout<<"delay["<<i<<"]:"<<delay<<endl;
//
//        for (int j=0;j<tempNetList[i].subNetList.size();++j){
//            for (int k=1;k<tempNetList[i].subNetList.size();++k){
//                if(j==parent[k]) break;
//            }
////printf("111\n");
//            int pos = j;
//            int pinnum=0,len_down=0;
//            if(size-1<0) size=0;
//
//            while(parent[pos]!=-1){
//
//                for (int l=0;l<tempNetList[i].subNetList.size();++l){
//                    if(l==pos&&tempNetList[i].subNetList[l].target.x==-2) pinnum++;
//                    if(l==pos&&tempNetList[i].subNetList[l].target.x!=-2) len_down+=len[l]/10*c0[tempNetList[i].subNetList[l].source.layers]/40;
//                }
//
//                delay +=len[pos]/10*(r0[tempNetList[i].subNetList[pos].source.layers]/40)*(len[pos]*c0[tempNetList[i].subNetList[pos].source.layers]/800+(size-1)*c_out*pinnum+len_down);
//                pos = parent[pos];
//            }
////printf("555\n");
//
//        }
////printf("666\n");
//        delay += (size-1)*r_driver*child;//驱动电阻*孩子敄1�7
//        cout<<"delay["<<i<<"]:"<<delay<<endl;
//        delete parent;
// break;
    }

//printf("total Iroute connect cost %f\n",totalIrouteCost/10);
//    cout<<"total delay "<<delay<<endl;
//	printf("total delay %f\n",delay);
    printf("total Iroute connect cost %f\n",totalIrouteCost/10);
    tempNetList.clear();

    return totalIrouteCost;///10
}
void ILPAssign::ILP_Test(){
    try{
// Create an environment
        GRBEnv env = GRBEnv(true);
//					env.set("LogFile", "ilp.log");
        env.start();
        int subpanel_intervalList_size = 10;
        int track_size = 4;
        int numofsegments = 10;
        vector<int> coord = {100,200,300,400,500,600,500,400,300,200};
        vector<vector<int>> blk_cost = {
                {0,0,0,0},
                {0,0,0,0},
                {0,0,0,0},
                {0,0,0,0},
                {0,0,0,0},
                {0,0,0,0},
                {0,0,0,0},
                {0,0,0,0},
                {0,0,0,0},
                {0,0,0,0}
        };
        vector<vector<int>> wlcost = {
                {10, 20, 30, 40},
                {50, 60, 70, 80},
                {90, 100, 110, 120},
                {130, 140, 150, 160},
                {170, 180, 190, 200},
                {210, 220, 230, 240},
                {250, 260, 270, 280},
                {290, 300, 310, 320},
                {330, 340, 350, 360},
                {370, 380, 390, 400}
        };
        vector<vector<bool>> c = {
                {0, 1, 0, 1, 1, 0, 1, 0, 0, 1},
                {1, 0, 1, 0, 0, 1, 0, 1, 1, 0},
                {0, 1, 0, 1, 0, 1, 1, 0, 1, 0},
                {1, 0, 1, 0, 1, 0, 0, 1, 0, 1},
                {1, 1, 0, 1, 0, 1, 0, 1, 0, 0},
                {0, 1, 1, 0, 1, 0, 1, 0, 1, 1},
                {1, 0, 1, 0, 0, 1, 0, 1, 0, 1},
                {0, 1, 0, 1, 1, 0, 1, 0, 1, 0},
                {0, 1, 1, 0, 1, 1, 0, 1, 0, 1},
                {1, 0, 0, 1, 0, 1, 1, 0, 1, 0}
        };
// Create an empty model
        GRBModel model = GRBModel(env);
        model.getEnv().set(GRB_IntParam_OutputFlag, 0);
//create variables
        GRBVar **x=0;//First declare an array pointing to an array
        x = new GRBVar*[subpanel_intervalList_size];
        for (int j=0;j<subpanel_intervalList_size;++j)//iroutes
        {
//create decision variables
            x[j] = model.addVars(track_size,GRB_BINARY);
        }

// Set objective: minimize blkcost+overlapcost

//calculate overlap cost
        OverlapResult overlapresult,blkresult,result;

        GRBVar **onum;
        onum = new GRBVar*[track_size];
        for(int t = 0;t<track_size;++t)
            onum[t] = model.addVars(numofsegments,GRB_INTEGER);


        result.overlapCount = 0;
        result.overlapLength = 0;
        result.overlapBlk = 0;

        GRBLinExpr cost1;//overlap cost
        cost1.clear();
//calculating overlap length
        for(int t = 0; t<track_size;++t){
            for(int k = 0;k<numofsegments;++k){
                cost1 += onum[t][k] * coord[k];
            }
        }


        GRBLinExpr cost2;//blockage cost
        cost2.clear();
//calculating blkcost
        for (int in = 0;in < subpanel_intervalList_size;in++) {
            for (int t = 0;t < track_size;t++) {
                cost2 += x[in][t] * blk_cost[in][t];
            }
        }


//wlcost
//        GRBLinExpr wlcost;
//        wlcost.clear();
//        wlcost = CalculateIrouteCost(subpanel,x);
        GRBLinExpr cost3;
        for (int in = 0;in < subpanel_intervalList_size;in++) {
            for (int t = 0;t < track_size;t++) {
                cost3 += x[in][t] * wlcost[in][t];
            }
        }

//GRBLinExpr cost = GetCost(result);
//cost1:olp;cost2,blk;wlcost
        model.setObjective(cost1+blk_cost_weight*cost2+wl_cost_weight*cost3, GRB_MINIMIZE);

//Add constraint

        GRBLinExpr c0, c1;
        c1.clear();

//constraint 1: every iroute a constraint;every constraint num of tracks size
        for(int in=0;in<subpanel_intervalList_size;in++)
        {
            for(int t=0;t<track_size;t++)
            {
                c1 = c1 + x[in][t];//constraint 0
            }
// Add constraint:丢�个iroute不可分给两个track
            model.addConstr(c1 == 1);
            c1.clear();

        }

//constraint 2/**/
//c0.clear();
        for(int t = 0;t<track_size;++t){
            for(int k = 0;k<numofsegments;++k){
//c0 = onum[t][k];
// Add constraint:keep the value of onum is greater than 0
                model.addConstr(onum[t][k] >= 0);//
//c0.clear();
            }

        }


//constraint 4
        GRBLinExpr c3;
        c3.clear();
        for(int t = 0;t<track_size;++t){
            for(int k = 0;k<numofsegments;++k){
                for(int in = 0;in<subpanel_intervalList_size;++in)
                    c3  += c[in][k] * x[in][t];
                c3 -= 1;
                model.addConstr(onum[t][k]>=c3);
                c3.clear();
            }
        }

        model.optimize();

        total_blkcost+=(cost2.getValue()/10);
        total_olpcost+=(cost1.getValue()/10);
        total_wlcost+=(cost3.getValue()/10);
        cost11 += (cost1.getValue()/10);
        cost22 += (cost2.getValue()/10);
        for (int ir=0;ir<subpanel_intervalList_size;ir++) {
            for (int t=0;t<track_size;t++) {
                std::cout << (x[ir][t].get(GRB_DoubleAttr_X )==1) <<" ";
            }
            std::cout << std::endl;
        }
    }catch(GRBException e){
        cout<<"Error code ="<<e.getErrorCode()<<endl;
        cout<<e.getMessage()<<endl;
    }
    catch(const std::exception& e)
    {   cout<<"Exception during optimization"<<endl;
        std::cerr << "Caught exception: " << e.what() << std::endl;
    }

}
void ILPAssign::DP_Test(){
    int subpanel_intervalList_size = 10;
    int track_size = 4;
    int numofsegments = 10;
    vector<int> coord = {100,200,300,400,500,600,500,400,300,200};
    vector<vector<bool>> x(subpanel_intervalList_size, vector<bool>(track_size, false));
    vector<vector<int>> blkcost = {
            {0,0,0,0},
            {0,0,0,0},
            {0,0,0,0},
            {0,0,0,0},
            {0,0,0,0},
            {0,0,0,0},
            {0,0,0,0},
            {0,0,0,0},
            {0,0,0,0},
            {0,0,0,0}
    };
    vector<vector<int>> wlcost = {
            {10, 20, 30, 40},
            {50, 60, 70, 80},
            {90, 100, 110, 120},
            {130, 140, 150, 160},
            {170, 180, 190, 200},
            {210, 220, 230, 240},
            {250, 260, 270, 280},
            {290, 300, 310, 320},
            {330, 340, 350, 360},
            {370, 380, 390, 400}
    };
    vector<vector<bool>> c = {
            {0, 1, 0, 1, 1, 0, 1, 0, 0, 1},
            {1, 0, 1, 0, 0, 1, 0, 1, 1, 0},
            {0, 1, 0, 1, 0, 1, 1, 0, 1, 0},
            {1, 0, 1, 0, 1, 0, 0, 1, 0, 1},
            {1, 1, 0, 1, 0, 1, 0, 1, 0, 0},
            {0, 1, 1, 0, 1, 0, 1, 0, 1, 1},
            {1, 0, 1, 0, 0, 1, 0, 1, 0, 1},
            {0, 1, 0, 1, 1, 0, 1, 0, 1, 0},
            {0, 1, 1, 0, 1, 1, 0, 1, 0, 1},
            {1, 0, 0, 1, 0, 1, 1, 0, 1, 0}
    };
    const int a = numofsegments / 32 + 1;
    vector<vector<int>> dp(subpanel_intervalList_size, vector<int>(track_size, 0));
    vector<vector<int>> flag(subpanel_intervalList_size, vector<int>(track_size, -1));
    vector<int> o_cost(track_size, 0);
    vector<vector<vector<unsigned int>>> onums(track_size,
                                               vector<vector<unsigned int>>(track_size,
                                                                            vector<unsigned int>(a, 0)));
    for (int row = 0; row < subpanel_intervalList_size; row++) {

        if (row == 0) {
            for (int column = 0; column < track_size; column++) {
                dp[0][column] = wlcost[0][column] * wl_cost_weight + blkcost[0][column] * blk_cost_weight;

                for (int k = 0; k < numofsegments; k++) {
                    if (c[row][k]) {
                        int elementIndex = k / 32;
                        int bitIndex = k % 32;
                        onums[column][column][elementIndex] |= (1u << bitIndex);
                    }
                }
            }
        } else {
            for (int column = 0; column < track_size; column++) {
                int lastCost = 2147483640;
                int lastIndex = 0;
                cout<<"(";
                for (int t = 0; t < track_size; t++) {
                    int changeCost = 0;
                    for (int k = 0; k < numofsegments; k++) {
                        int elementIndex = k / 32;
                        int bitIndex = k % 32;
                        if (c[row][k] && ((onums[t][column][elementIndex] & (1u << bitIndex)) != 0)) {
                            changeCost += coord[k] ;
                        }
                    }
                    cout<<changeCost<<",";
                    if (
                            changeCost +
                            dp[row - 1][t] < lastCost) {
                        lastCost =
                                changeCost +
                                dp[row - 1][t];
                        lastIndex = t;
                    }
                }

//                            cout<<"lastCost"<<lastCost<<endl;
                flag[row][column] = lastIndex;
//                            cout<<"lastIndex"<<lastIndex<<endl;
                cout<<")";
            }

            vector<int> copy_o_cost(o_cost);
            vector<vector<vector<unsigned int>>> copiedOnums(onums);
            for (int j = 0; j < track_size; j++) {
                onums[j] = copiedOnums[flag[row][j]];
                o_cost[j] = copy_o_cost[flag[row][j]];
                int o_cost2 = 0;
                for (int k = 0; k < numofsegments; k++) {
                    int elementIndex = k / 32;
                    int bitIndex = k % 32;
                    if (c[row][k]) {
                        if ((onums[j][j][elementIndex] & (1u << bitIndex)) != 0) {
                            o_cost[j] += coord[k];
                            o_cost2 += coord[k];
                        }
                        onums[j][j][elementIndex] |= (1u << bitIndex);
                    }
                }
                dp[row][j] = wl_cost_weight * wlcost[row][j] + blk_cost_weight * blkcost[row][j]
                             + o_cost2 + dp[row - 1][flag[row][j]];
            }
            cout<<endl;
        }
    }
    int minIndex = 0;

    for (int j = 0; j < track_size; j++) {
        if (dp[subpanel_intervalList_size - 1][minIndex] > dp[subpanel_intervalList_size - 1][j]) {
            minIndex = j;
        }
    }

    x[subpanel_intervalList_size - 1][minIndex] = 1;
    int blk_temp = 0;
    int wl_temp = 0;
    int ol_temp = 0;
    wl_temp += (wlcost[subpanel_intervalList_size - 1][minIndex]);
    ol_temp += (o_cost[minIndex]);
    blk_temp += blkcost[subpanel_intervalList_size - 1][minIndex];

    for (int in = subpanel_intervalList_size-1 ; in > 0; in--) {
        minIndex = flag[in][minIndex];
        x[in - 1][minIndex] = 1;
        wl_temp += (wlcost[in-1][minIndex] );
        blk_temp += blkcost[in-1][minIndex] ;
    }

    total_blkcost+= (blk_temp/10);
    total_olpcost += (ol_temp/10);
    total_wlcost += wl_temp/10;
    cost11 += ol_temp/10;
    cost22 += blk_temp/10;

    cout << "xxx" << endl;
    for (const auto &row: x) {
        for (auto value: row) {
            std::cout << value << " ";
        }
        std::cout << std::endl;  // 每行结束后换衄1�7
    }

}

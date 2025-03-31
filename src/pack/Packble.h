#ifndef _PACKBLE_H_
#define _PACKBLE_H_
#include "../Database.h"
#include "ble.h"
#include "matching.h"
using namespace db;
typedef unsigned short conn_deg_type;
enum InstType { LUT1 = 1, LUT2 = 2, LUT3 = 3, LUT4 = 4, LUT5 = 5, LUT6 = 6, FF = 7, OTHERS = 8 };

void packble(vector<Group>& groups);
class PackBLE {
private:
    vector<BLE*> bles;  // result
    vector<BLE*> inst2ble;
    vector<Point2d> instPoss;  // for GP
    vector<std::vector<std::vector<BLE*> > > table2Ble; // 三重数组
    unsigned int numInst;       // num of instances
    unsigned int numLUT;        // num of LUT
    unsigned int numFF;         // num of FF
    vector<InstType> instType;  // instance type

    unsigned int num2LUTBle;
    unsigned int num2FFBle;
    unsigned int numPackedFF;

    const double mergeRange = 20;  // x/2+y
    unordered_set<Net*> ignoreNetId;
    vector<unordered_map<unsigned int, conn_deg_type>>
        table1StepConn;  // 1-step connection degree (ignore FF-FF connections via clk, sr, ce)

    BLE* NewBle();
    void DeleteBle(BLE* ble);
    void AddLUT(BLE* ble, Instance* lut);
    void AddFF(BLE* ble, Instance* ff);
    void MergeBLE(BLE* ble1, BLE* ble2);
    BLE *getBLE(int x, int y, int z);
    void setBLE(int x, int y, int z, BLE* ble);

public:
    PackBLE(vector<Group>& groups);
    ~PackBLE();

    void StatFFType() const;
    void StatLUT2FF() const;
    void SiteMapBle(Pack *pack, BLE *ble, int bank, bool getpair);
    void PairLUTFF();
    bool isRealpairFF(BLE* ble, Instance* lut);
    bool isRealpairLUT(BLE* ble, Instance* ff);

    int withNearLut(Instance* lut);
    void PairLUTs();
    void PackSingFF();

    void GetResult(vector<Group>& groups) const;
};
#endif


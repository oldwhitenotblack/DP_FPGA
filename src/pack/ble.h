#ifndef _BLE_H_
#define _BLE_H_
#include "../Database.h"
using namespace db;
class BleType {  // clk, sr should always be checked after ce, in case that ce.size()==0
public:
    Net* clk;
    Net* sr;
    vector<Net*> ce;  // always ordered for a ble
    // for dev
    void Print() const;
};
// For BleGroup
bool IsMatched(BleType& type1, BleType& type2);

class CLB1;
class BLE {
    friend class BleGroup;
    friend class CLB1;

private:
    unsigned int id;
    CLB1* clb = NULL;

    vector<Instance*> LUTs;  // max size 8
    vector<Instance*> FFs;   // max size 8
    BleType type;
    BLE& operator=(const BLE& rhs);

public:
    BLE(unsigned int i = 0) { id = i; }
    int x,y,bank;
    // read only
    const unsigned int& id_ = id;
    // CLB1* const & clb_ = clb;
    vector<Instance*>& LUTs_ = LUTs;
    vector<Instance*>& FFs_ = FFs;
    const BleType& type_ = type;
    bool realPairLut = false;
    bool realPairDff[8][8] = {false};

    void SetId(unsigned int i) { id = i; }
    void AddLUT(Instance* inst);
    void AddFF(Instance* inst);
    void Remove(Instance* inst);
    bool IsLUTCompatWith(BLE* rhs);
    bool IsFFCompatWith(BLE* rhs);
    void MergeInto(BLE* ble);
    void Empty();
    void Print() const;
    void GetResult(Group& group) const;

    // for bles
    static void StatBles(const vector<BLE*>& bles);
    static void CheckBles(const vector<BLE*>& bles);
};
#endif
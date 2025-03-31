#include "../include/global.h"
#include "../Database.h"
#include "Packble.h"
#define MAX_GRAPH 2000
vector<bool> with2FF;

void packble(vector<Group>& groups) {
    PackBLE packble(groups);
    packble.PairLUTFF();
    packble.PairLUTs();
    packble.PackSingFF();
    // packble.GetResult(groups);
}
PackBLE::PackBLE(vector<Group>& groups) {
    numInst = database.instances.size();
    num2LUTBle = 0;
    num2FFBle = 0;
    numPackedFF = 0;
    inst2ble = vector<BLE*>(numInst, NULL);

    // 把赛题给定的布局结果绑定到instposs从而初始化
    instPoss = vector<Point2d>(numInst);
    for (unsigned int i = 0; i < numInst; ++i) {
        instPoss[i].x = groups[i].x;
        instPoss[i].y = groups[i].y;
        // instPoss[i].z = groups[i].z;
    }
    groups.clear();

    // 遍历所有instance记录lut和dff的个数同时用instType将instance的类型和其下标绑定
    numLUT = 0;
    numFF = 0;
    instType = vector<InstType>(numInst, OTHERS);
    for (unsigned int i = 0; i < numInst; ++i) {
        auto name = database.instances[i]->master->resource->name;
        if (name == Resource::LUT6) {
            ++numLUT;
            int value = database.instances[i]->master->name - Master::LUT1;
            if (value < 7) {
                // 跟ripple不一样，这个lutOPinIdx在lut1-6是不一样的...挺无语的
                database.instances[i]->lutOPinIdx = database.instances[i]->master->pins.size()-1;
                if (database.instances[i]->master->name == Master::LUT6X) value--;
                instType[i] = InstType(value + 1);
            } else { //对其他的比如rama都当做OTHERS
                instType[i] = InstType(8);
            }
        } else if (name == Resource::DFF) {
            instType[i] = FF;
            ++numFF;
        }
    }
    // printlog(LOG_INFO, "PackBLE: inst #:\t%d", numInst);
    // printlog(LOG_INFO, "PackBLE: FF #:\t%d", numFF);
    // printlog(LOG_INFO, "PackBLE: LUT #:\t%d", numLUT);

    // ignore clk, sr, ce
    for (auto inst : database.instances)
        if (inst->IsFF()) {
            ignoreNetId.insert(inst->pins[database.clkPinIdx]->net);
            ignoreNetId.insert(inst->pins[database.srPinIdx]->net);
            ignoreNetId.insert(inst->pins[database.cePinIdx]->net);
        }

    // 1-step connection degree (ignore FF-FF connections via clk, sr, ce)
    /*遍历每个网表，对于非连接FF（只连接dataPin)的网表中的pins，
        标记他们的degree值以便他们可以在通过一步打包成ble*/ 
    // int iteranum = 246; //测试xx网表情况
    table1StepConn = vector<unordered_map<unsigned int, conn_deg_type>>(numInst);
    for (auto net : database.nets) {
        // if (net->id == iteranum) {
            if (ignoreNetId.find(net) == ignoreNetId.end()) {
                for (auto itPin1 = net->pins.begin(); itPin1 != net->pins.end(); ++itPin1) {
                    unsigned int id1 = (*itPin1)->instance->id;
                    // cout << (*itPin1)->instance->name << ":" << Resource::NameEnum2String((*itPin1)->instance->master->resource->name) << "," << instType[(*itPin1)->instance->id] << endl;
                    for (auto itPin2 = next(itPin1); itPin2 != net->pins.end(); ++itPin2) {
                        unsigned int id2 = (*itPin2)->instance->id;
                        // cout << (*itPin2)->instance->name << ":" << instType[(*itPin2)->instance->id] << endl;
                        ++table1StepConn[id1][id2];
                        ++table1StepConn[id2][id1];
                    }
                }
            }
        // }
    }

    unsigned int num1StepConnPair = 0;
    conn_deg_type max1StepConn = 0;
    for (const auto& adjs : table1StepConn) {
        num1StepConnPair += adjs.size();
        for (const auto& conn : adjs)
            if (conn.second > max1StepConn) max1StepConn = conn.second;
    }
    table2Ble.resize(database.sitemap_nx);
    for (auto& row : table2Ble) {
        row.resize(database.sitemap_ny, vector<BLE*>(2, NULL));
    }
    cout << "packble init!" << endl;
    // printlog(LOG_INFO, "PackBLE: 1-step connected pair #:\t%d", num1StepConnPair / 2);
    // printlog(LOG_INFO, "PackBLE: max 1-step connection:\t%d", max1StepConn);
}
PackBLE::~PackBLE() {
    for (auto& ble : bles) delete ble;
}
BLE* PackBLE::NewBle() {
    BLE* newB = new BLE(bles.size());
    bles.push_back(newB);
    return newB;
}
void PackBLE::DeleteBle(BLE* ble) {
    // reduce the size of bles by 1
    bles.back()->SetId(ble->id_);
    bles[ble->id_] = bles.back();
    bles.pop_back();
    // delete the content pointed by ble
    delete ble;
}
void PackBLE::AddLUT(BLE* ble, Instance* lut) {
    ble->AddLUT(lut);
    inst2ble[lut->id] = ble;
    ++num2LUTBle;
}

void PackBLE::AddFF(BLE* ble, Instance* ff) {
    // cout << ff->name << endl;
    ble->AddFF(ff);
    inst2ble[ff->id] = ble;
    ++num2FFBle;
}
void PackBLE::MergeBLE(BLE* ble1, BLE* ble2) {  // merge ble2 to ble1
    for (auto lut : ble2->LUTs_) AddLUT(ble1, lut);
    for (auto ff : ble2->FFs_) AddFF(ble1, ff);
    if (ble2->LUTs_.size() == 8) --num2LUTBle;
    if (ble2->FFs_.size() == 8) --num2FFBle;
    DeleteBle(ble2);
}
BLE *PackBLE::getBLE(int x, int y, int z) {
    if (x > database.sitemap_nx || y > database.sitemap_ny) return NULL;
    if (x < 0 || y < 0) return NULL;
    return table2Ble[x][y][z];
}
void PackBLE::setBLE(int x, int y, int z, BLE *ble) {
    if (x > database.sitemap_nx || y > database.sitemap_ny) return;
    if (x < 0 || y < 0) return;
    if (x == 79 && y == 283) {
        cout << "setble:" << x << ',' << y << ',' << z << ',' << ble << endl;
        for (auto lut : ble->LUTs_) {
            if (lut!= NULL) cout << lut->name << endl;
        }
    }
    table2Ble[x][y][z] = ble;
}   
//*******************************************************************************
void PackBLE::StatFFType() const {
    unordered_set<Net*> clkNet, srNet, ceNet;
    // unordered_map<pair<Net*, Net*>, unsigned int, boost::hash<pair<Net*, Net*>>> srceNet;
    for (auto inst : database.instances)
        if (instType[inst->id] == FF) {
            clkNet.insert(inst->pins[database.clkPinIdx]->net);
            srNet.insert(inst->pins[database.srPinIdx]->net);
            ceNet.insert(inst->pins[database.cePinIdx]->net);
            // ++srceNet[make_pair(inst->pins[database.srPinIdx]->net, inst->pins[database.cePinIdx]->net)];
        }
    // printlog(LOG_INFO, "StatFFType: clk type #:\t%d", clkNet.size());
    // printlog(LOG_INFO, "StatFFType: sr type #:\t%d", srNet.size());
    // printlog(LOG_INFO, "StatFFType: ce type #:\t%d", ceNet.size());
    // printlog(LOG_INFO, "StatFFType: sr&ce type #:\t%d", srceNet.size());
}
void PackBLE::StatLUT2FF() const {
    // 实际上是统计，没软子用
    vector<int> numFF;
    int num2PinNet = 0;
    for (auto inst : database.instances) {
        // cout << inst->name << ',' << inst->pins[inst->lutOPinIdx] << endl;
        if (instType[inst->id] <= LUT6) { //若instance为lut
            Net* oNet = inst->pins[inst->lutOPinIdx]->net; //onet为instance的outputPin所连接的net
            if (oNet == NULL) {
                continue;
            }
            // cout << oNet->name << ',' << inst->pins[inst->lutOPinIdx]->instance->name << endl;
            unsigned int nFF = 0;
            for (auto pin : oNet->pins) {//遍历onet的每个pin然后找ff类型的instance且pin的为数据端
                // cout << pin->instance->name << endl;
                if (instType[pin->instance->id] == FF 
                    && pin->type->type == "INPUT" && pin->type->arr=="") {
                    // cout << ":" << Master::NameEnum2String(pin->instance->master->name) << endl;
                    nFF++;
                } 
            }
            if (nFF >= numFF.size()) numFF.resize(nFF + 1, 0);
            ++numFF[nFF];//记录net中lut作为输出连接到dff的网表个数
            if (nFF == 1 && oNet->pins.size() == 2) num2PinNet++;
        }
    }
    int numxFF = 0;
    for (unsigned int i = 1; i < numFF.size(); ++i) numxFF += numFF[i];
    for (unsigned int i = 0; i < numFF.size(); ++i) {
        cout << "statlut2f:" << i << "-ff lut #:" << numFF[i] << endl;
        // printlog(LOG_INFO, "StatLUT2FF: %d-FF LUT #:\t%d", i, numFF[i]);
    }
    cout << "StatLUT2FF: only-1-FF LUT #:" << num2PinNet << endl;
    // printlog(LOG_INFO, "StatLUT2FF: only-1-FF LUT #:%d", num2PinNet);
}
void PackBLE::SiteMapBle(Pack *pack, BLE *ble, int bankz, bool getpair) {
    // 将lut周围的instance加入到ble中
    Pack *plbpack = pack;
    int lutl = 0, luth = 7, dffl = 16, dffh = 23;
    if (bankz == 1) {
        lutl = 8;
        luth = 15;
        dffl = 24;
        dffh = 31;
    }
    if (getpair == false) {
        for (int j = dffl; j <= dffh; j++) {
            if (plbpack->instances[j]!=NULL) {
                // cout << plbpack->instances[j]->name << endl;
                AddFF(ble,plbpack->instances[j]);
            }
        }
        for (int i = lutl; i <= luth; i++) {
            if (plbpack->instances[i]!=NULL) {
                if (pack->site->x == 79 && pack->site->y == 283) {
                    cout << plbpack->instances[i]->name << endl;
                }
                AddLUT(ble,plbpack->instances[i]);
                if (ble->FFs_.size() == 8) {
                    with2FF[plbpack->instances[i]->id] = true;
                }
            }
        }
    }
    for (int i = 0; i < 8; i++) {
        if (plbpack->instances[lutl+i]==NULL) continue;
        Instance *ins = plbpack->instances[lutl+i];
        if (ins->pins[ins->lutOPinIdx]->net == NULL || ins->pins[ins->lutOPinIdx]->net == NULL) continue;
        for (int j = 0; j < 8; j++) { 
            if (plbpack->instances[dffl+j]==NULL || plbpack->instances[dffl+j]->pins[database.ffIPinIdx]->net == NULL) continue;
            Instance *ins2 = plbpack->instances[dffl+j];
            if (ins->pins[ins->lutOPinIdx]->net->name == ins2->pins[database.ffIPinIdx]->net->name) {
                ble->realPairDff[i][j] = true;
            } else {
                ble->realPairDff[i][j] = false;
            }
            
        }
    }
}
void PackBLE::PairLUTFF() {
    // StatFFType();
    // StatLUT2FF();
    unsigned numInciLUT = 0;
    unsigned numTooFarFF = 0;
    unsigned numThrownFF = 0;
    unsigned numPackedLUT = 0;
    with2FF = vector<bool>(numInst, false);
    for (unsigned int i = 0; i < numInst; ++i)
        if (instType[i] <= LUT6 || database.instances[i]->master->name == Master::LUT6X) {
            Instance* lut = database.instances[i];
            // if (i > 22132) {
                // cout << "lut " << lut->name << ',' << lut->x << ',' << lut->y << ',' << lut->bankz << ',' << lut->packz << endl;
            // }
            vector<pair<double, unsigned int>> cands; //候选集
            BLE* newBle;
            if (getBLE(lut->x, lut->y, lut->bankz)!=NULL) {// 防止重复new ble
                // 至少有两lut在ble中了
                newBle = inst2ble[database.instances[i]->id];
            } else {
                newBle = NewBle(); // 先new一个ble然后加入lut
                // AddLUT(newBle, lut);
                newBle->x = lut->x;
                newBle->y = lut->y;
                newBle->bank = lut->bankz;
                setBLE(newBle->x, newBle->y, newBle->bank, newBle);
                SiteMapBle(lut->pack,newBle,lut->bankz,false); // 把lut周围的instance添加置ble中
            }
            // cout << newBle->LUTs_[0]->name << endl;            
            if (with2FF[i]==true) continue; //如果周围已经有了8个ff就不用判断了 直接下一个
            // cout << ':' << lut->name << ',' << with2FF[i]<< endl;
            if (lut->pins[lut->lutOPinIdx]->net == NULL) continue;
            for (auto pin : lut->pins[lut->lutOPinIdx]->net->pins) {
                unsigned int idFF = pin->instance->id;
                if (instType[idFF] == FF && pin->type->type == "INPUT" && pin->type->arr=="") {
                    // 因为看了测试用例发现seq不会被fixed
                    // cout << pin->instance->name << ":" << Master::NameEnum2String(pin->instance->master->name) << endl;
                    /*从lut的x/2+y距离<20的范围选定候选ff，其实可以再加个判断是否在同一个clk region*/ 
                    if (inst2ble[idFF] != NULL && 
                        isRealpairLUT(inst2ble[idFF], database.instances[idFF])) continue;
                    double dist = 
                        abs(instPoss[idFF].x - instPoss[lut->id].x) / 2 
                            + abs(instPoss[idFF].y - instPoss[lut->id].y)
                                + abs(instPoss[idFF].z)+ 0.1;
                    // cout << "lut" << lut->name << "," << pin->instance->name << ":" << dist << endl;
                    // if (dist <= mergeRange) // 如果是已经在同一个site的话就不算了 所以大于1
                    cands.push_back(make_pair(dist, idFF));
                    // else
                    //     ++numTooFarFF;
                }
            }
            if (cands.size() != 0) {
                ++numInciLUT; //记录能够绑定的lut，统计的没啥用
                sort(cands.begin(), cands.end());  // TODO: num of FF, sum of disp? 根据距离排序
                Instance* ff0 = NULL;
                unsigned char c = 0;
                if (newBle->FFs_.size() == 0) {
                    // 对ble中的dff为空的情况
                    for (;c < cands.size(); c++) {
                        //对最近的dff选择加入到lut所在的site中
                        if (database.canMoveInstance(lut->pack,database.instances[cands[c].second],-1,lut->bankz)) {
                            //这个方法里面概括了src、ce判定，容量判断等
                            ff0 = database.instances[cands[c].second];
                            if (inst2ble[database.instances[cands[c].second]->id] != NULL) {
                                inst2ble[database.instances[cands[c].second]->id]->Remove(ff0);
                                inst2ble[database.instances[cands[c].second]->id] = NULL;
                            }
                            AddFF(newBle, ff0);
                            ++numPackedFF;
                            SiteMapBle(ff0->pack,newBle,newBle->bank,true);
                            if (with2FF[i] == true) {
                                for (auto ins : newBle->LUTs_) {
                                    if (ins!=NULL) with2FF[ins->id] = true;
                                }
                                c++;
                                break;
                            }
                        } 
                    }
                    if (ff0 == NULL) continue;
                } else {
                    // 对ble中dff含有至少一个
                    ff0 = newBle->FFs_[0];
                }
                assert(ff0!=NULL);
                for (; c < cands.size(); ++c) {// 选择其他dff加入
                    Instance* ff1 = database.instances[cands[c].second];
                    // if (ff1->pins[database.clkPinIdx]->net == ff0->pins[database.clkPinIdx]->net &&
                    //     ff1->pins[database.srPinIdx]->net == ff0->pins[database.srPinIdx]->net) {
                    // if (lut->name == "inst_22133") {
                    //     cout << ff1->name << ',' << ff1->x << ',' <<ff1->y << ff1->bankz << endl;
                    // }
                    if (ff1->bankz != newBle->bank) {
                        BLE* newBle2 = getBLE(newBle->x, newBle->y, ff1->bankz);
                        if (newBle2 == NULL) {
                            newBle2 = NewBle();
                            newBle2->x = newBle->x;
                            newBle2->y = newBle->y;
                            newBle2->bank = ff1->bankz;
                            setBLE(newBle2->x, newBle2->y, newBle2->bank, newBle2);
                            SiteMapBle(lut->pack,newBle2,ff1->bankz,false);
                        }
                        if (newBle2->FFs_.size() == 8) {
                            for (auto ins : newBle2->LUTs_) {
                                if (ins!=NULL) with2FF[ins->id] = true;
                            }
                            break;
                        } else {
                            if (database.canMoveInstance(lut->pack,ff1,-1,lut->bankz)) {
                                if (inst2ble[ff1->id]!= NULL) {
                                    // 去除原ble中的ff
                                    inst2ble[ff1->id]->Remove(ff1);
                                    inst2ble[ff1->id] = NULL;
                                }
                                AddFF(newBle, ff1);
                                ++numPackedFF;
                                SiteMapBle(lut->pack,newBle,lut->bankz,true);
                                if (newBle->FFs_.size() == 8) {
                                    for (auto ins : newBle->LUTs_) {
                                        if (ins!=NULL) with2FF[ins->id] = true;
                                    }
                                    break;
                                }
                                continue;
                            } else {
                                if (database.canMoveInstance(lut->pack,ff1,-1,ff1->bankz)) {
                                    if (inst2ble[ff1->id]!= NULL) {
                                        // 去除原ble中的ff
                                        inst2ble[ff1->id]->Remove(ff1);
                                        inst2ble[ff1->id] = NULL;
                                    }
                                    AddFF(newBle2, ff1);
                                    ++numPackedFF;
                                    SiteMapBle(ff1->pack,newBle2,ff1->bankz,true);
                                    if (newBle2->FFs_.size() == 8) {
                                        for (auto ins : newBle2->LUTs_) {
                                            if (ins!=NULL) with2FF[ins->id] = true;
                                        }
                                        break;
                                    }
                                    continue;
                                } else {
                                    ++numThrownFF; 
                                }
                            }
                        }    
                    } 
                    else {
                        if (database.canMoveInstance(ff0->pack,ff1,-1,ff0->bankz)) {
                            if (inst2ble[ff1->id] != NULL) {
                                inst2ble[ff1->id]->Remove(ff1);
                                inst2ble[ff1->id] = NULL;
                            }
                            AddFF(newBle, ff1);
                            ++numPackedFF;
                            SiteMapBle(lut->pack,newBle,ff1->bankz,true);
                            if (newBle->FFs_.size()==8) {//表示这个lut所构成的ble已经有8个dff了
                                for (auto ins : newBle->LUTs_) {
                                    if (ins!=NULL) with2FF[ins->id] = true;
                                }
                                break;
                            }
                        } else {
                            ++numThrownFF;
                        }
                    }
                }
                // 就是说上述操作完毕后发现这个lut还是没有找到可以配对的ff，就要把想办法把当前的lut移动到其他ble中
                if (isRealpairFF(newBle, lut) == false && lut->fixed == false) {
                    for (int i = 0; i < cands.size(); i++) {
                        Instance* ff2 = database.instances[cands[i].second];
                        if (inst2ble[ff2->id]!=NULL && isRealpairLUT(inst2ble[ff2->id],ff2)) continue;
                        if (inst2ble[ff2->id]!=NULL && inst2ble[ff2->id]->LUTs_.size() == 8) continue;
                        if (database.canMoveInstance(ff2->pack,lut,-1,ff2->bankz)) {
                            newBle->Remove(lut);
                            // SiteMapBle(lut->pack,newBle,lut->bankz,true); 这段可以不用，因为lut本来就是no pair的
                            inst2ble[lut->id] = NULL;
                            if (inst2ble[ff2->id]!=NULL) {
                                SiteMapBle(ff2->pack,inst2ble[ff2->id],ff2->bankz,true);
                            }
                            numPackedLUT++;
                            break;
                        }
                    }
                }
            }
            // cout << "over" << endl;
        }
    cout << "PairLUTFF:" << numPackedFF << "FFs packed, " << numInciLUT << "LUTs incident, " << "LUTs packed to FF:" << numPackedLUT << endl;
    cout << "PairLUTFF: BLE #: " << bles.size() << ", 2-FF BLE #: " 
        << num2FFBle << ", thrown FF #: " << numThrownFF << ", too far FF#: " << numTooFarFF<<endl;
}
bool PackBLE::isRealpairFF(BLE* ble, Instance* lut) {
    int blepackz = lut->packz;
    if (lut->bankz == 1) { // 对于bank1的lut，他的z坐标是相对于bank0的，所以要减去8
        blepackz -= 8; 
    }
    for (auto pairdff : ble->realPairDff[blepackz]) 
        if (pairdff == true) return false;
    return true;
}
bool PackBLE::isRealpairLUT(BLE* ble, Instance* ff) {
    int blepackz = ff->packz;
    if (ff->bankz == 1) { // 对于bank1的lut，他的z坐标是相对于bank0的，所以要减去16
        blepackz -= 16; 
    }
    for (int j = 0; j < 8; j++) 
        if (ble->realPairDff[j][blepackz] == true) return false;
    return true;
}
int PackBLE::withNearLut(Instance* lut) {
    if (lut->packz % 2 == 0) {
        if (lut->pack->instances[lut->packz+1] != NULL) {
            return -1;
        }
        return lut->packz+1;
    } else {
        if (lut->pack->instances[lut->packz-1] != NULL) {
            return -1;
        }
        return lut->packz-1;
    }
}
void PackBLE::PairLUTs() {
    // is valid connected pair 这步就是找能相互配对的lut
    unsigned int numValidConnPair = 0;
    unsigned int numPackedLUT = 0;
    vector<unsigned int> countVCP;
    for (unsigned int i = 0; i < numInst; ++i) {  // instance i ，找lut1-5 因为他们最多只占用半个lut6
        if (instType[i] > LUT5 || withNearLut(database.instances[i]) == -1) continue; //case_1 的inst_1185的output为悬空引脚
        BLE *ble = NULL;
        if (inst2ble[database.instances[i]->id] == NULL) {
            ble = NewBle();
            ble->x = database.instances[i]->x;
            ble->y = database.instances[i]->y;
            ble->bank = database.instances[i]->bankz;
            setBLE(ble->x, ble->y, ble->bank, ble);
            SiteMapBle(database.instances[i]->pack,ble,database.instances[i]->bankz,false);
        } else {
            ble = inst2ble[database.instances[i]->id];
        }
        assert(ble!=NULL);
        if (ble->LUTs_.size() == 8) continue;
        bool pairsuc = false;
        for (auto neigh : table1StepConn[i]) {  // instance j ，这就是寻找与i的Pin相连的net中的lut1-5
            auto j = neigh.first;
            // cout << database.instances[j]->name << endl;
            // if ((inst2ble[j])==0) continue; // 处理为空的引脚，因为赛题存在悬空output
            if (instType[j] > LUT5 || j < i)
                continue;               // 与之配对的lut必须是lut1-5，且没有与自身所在ble中的dff有连接关系
            BLE *ble2 = NULL;
            if (inst2ble[database.instances[j]->id]==NULL) {
                ble2 = NewBle();
                ble2->x = database.instances[j]->x;
                ble2->y = database.instances[j]->y;
                ble2->bank = database.instances[j]->bankz;
                setBLE(ble2->x, ble2->y, ble2->bank, ble2);
                SiteMapBle(database.instances[j]->pack,ble2,database.instances[j]->bankz,false);
            } else {
                ble2 = inst2ble[database.instances[j]->id];    
            }
            assert(ble2!=NULL);
            if (ble2->LUTs_.size() == 8) continue;
            if (isRealpairFF(ble, database.instances[i]) && isRealpairFF(ble2, database.instances[j])) continue;
            if (withNearLut(database.instances[j]) != -1) {
                auto conn1 = neigh.second;  // num of 1-step conn1就是共同连接的引脚连接数
                /*tot为总引脚数，包括输入输出的，然后减去共连的不能超过lut6的总引脚数*/
                auto tot = database.instances[i]->pins.size() + database.instances[j]->pins.size();
                if ((tot - conn1) > 8) continue;  // fast pruning for constraint 2 就是他们包括共享的总引脚是否超过8个(输入6输出2)
                auto numDupInputs = database.NumDupInputs(*database.instances[i], *database.instances[j]);  // reduce routing demand
                auto numDistInputs = tot - 2 - numDupInputs; //为啥不用con1 因为con1忽略了sr和clk引脚
                // cout << "inst_" << i << ':' ;
                // cout << "inst_" << j << ':' ;
                // cout << "tot：" << tot << ',' << "conn1:" << conn1 << ',' << "numDupInputs:" << numDupInputs << ',' << "numDistInputs:" << numDistInputs << endl;
                if (numDistInputs <= 6) {  // constraint 2: 合并后的引脚数量应当小于等于6
                    double dist = abs(instPoss[i].x - instPoss[j].x) / 2 + abs(instPoss[i].y - instPoss[j].y);
                    if (numDupInputs > 1 && dist < mergeRange) {  // constraint 3&4: shared inputs, distance
                        int movesitei = withNearLut(database.instances[i]);
                        int movesitej = withNearLut(database.instances[j]);
                        if (isRealpairFF(ble, database.instances[j]) && !isRealpairFF(ble2, database.instances[i])) {
                            // 如果j能够与其所以在的ble的dff配对，而i不能；
                            if (database.canMoveInstance(database.instances[j]->pack,database.instances[i], movesitej,database.instances[j]->bankz)) {
                                AddLUT(ble2, database.instances[i]);
                                ble->Remove(database.instances[i]);
                                ++numPackedLUT;
                                SiteMapBle(database.instances[j]->pack,ble,database.instances[j]->bankz,true);
                                break;
                            }
                        } else {
                            // 如果i只占用一个z坐标能够与其所以在的ble的dff配对，而j不能;或者都能够匹配，那么就把j放到i所在的site中
                            if (database.canMoveInstance(database.instances[i]->pack,database.instances[j], movesitei,database.instances[i]->bankz)) {
                                AddLUT(ble, database.instances[j]);
                                ble2->Remove(database.instances[j]);
                                ++numPackedLUT;
                                SiteMapBle(database.instances[i]->pack,ble,database.instances[i]->bankz,true);
                                break;
                            }
                        }
                    }
                    // stat
                    // ++numValidConnPair;
                    // if (conn1 >= countVCP.size()) countVCP.resize(conn1 + 1, 0);
                    // ++countVCP[conn1];
                }
            } else {
                // if (database.instances[j]->name == "inst_2243" || database.instances[i]->name == "inst_2243") {
                //     cout << database.instances[i]->name << ':' << database.instances[i]->x << ',' << database.instances[i]->y << ',' << database.instances[j]->name << ":" << database.instances[j]->x << ',' << database.instances[j]->y << '|' << withNearLut(database.instances[i]) << withNearLut(database.instances[j]) << endl; 
                // }
                // 尝试把当前的lut放到ble中其他位置
                if (isRealpairFF(ble, database.instances[j]) && !isRealpairFF(ble2, database.instances[i])) {
                    if (database.canMoveInstance(database.instances[j]->pack,database.instances[i], -1, database.instances[j]->bankz)) {
                        AddLUT(ble2, database.instances[i]);
                        ble->Remove(database.instances[i]);
                        ++numPackedLUT;
                        SiteMapBle(database.instances[j]->pack,ble,database.instances[j]->bankz,true);
                        break;
                    }
                } else {
                    if (database.canMoveInstance(database.instances[i]->pack,database.instances[j], -1, database.instances[i]->bankz)) {
                        AddLUT(ble, database.instances[j]);
                        ble2->Remove(database.instances[j]);
                        ++numPackedLUT;
                        SiteMapBle(database.instances[i]->pack,ble,database.instances[i]->bankz,true);
                        break;
                    }
                    
                }
            }
        }
    }
    cout << "PairLUTs: stage 1: there are " << numPackedLUT << " conneted pairs" << endl;
}
void PackBLE::PackSingFF() {
    int numPackedSingFF = 0;
    for (unsigned int i = 0; i < numInst; ++i)
        if (inst2ble[i] == NULL && instType[i] == FF) {
            BLE* newBle = NewBle();
            newBle->x = database.instances[i]->x;
            newBle->y = database.instances[i]->y;
            newBle->bank = database.instances[i]->bankz;
            // AddFF(newBle, database.instances[i]);
            setBLE(newBle->x, newBle->y, newBle->bank, newBle);
            SiteMapBle(database.instances[i]->pack,newBle,newBle->bank,false);
            if (newBle->FFs_.size() == 8) continue;
            for (unsigned int j = i+1; j < numInst; ++j) {
                if (inst2ble[j] == NULL && instType[j] == FF) {
                    if (database.instances[j]->bankz != newBle->bank) continue;
                    if (database.canMoveInstance(database.instances[i]->pack,database.instances[j], -1, database.instances[i]->bankz)) {
                        AddFF(newBle, database.instances[j]);
                        ++numPackedSingFF;
                        SiteMapBle(database.instances[i]->pack,newBle,newBle->bank,true);
                        if (newBle->FFs_.size() == 8) {
                            for (auto ins : newBle->LUTs_) {
                                if (ins!=NULL) with2FF[ins->id] = true;
                            }
                            break;
                        }
                    }
                }
            }
        }
    cout << "PackSingFF:" << numPackedSingFF << endl;
    // printlog(LOG_INFO, "PackSingFF: BLE #: %d (added BLEs are soft)", bles.size());
}
void PackBLE::GetResult(vector<Group>& groups) const {
    assert(groups.size() == 0);
    // BLE::CheckBles(bles);
    // BLE::StatBles(bles);

    for (auto ble : bles) {
        assert(ble->LUTs_.size() != 0 || ble->FFs_.size() != 0);
        Group group;
        ble->GetResult(group);
        group.id = groups.size();
        if (group.instances.size() == 1) {
            group.y = instPoss[group.instances[0]->id].y;
            group.x = instPoss[group.instances[0]->id].x;
        } else {
            group.y = 0;
            group.x = 0;
            unsigned int num = 0;
            for (auto inst : group.instances)
                if (inst->IsLUT()) {
                    group.y += instPoss[inst->id].y;
                    group.x += instPoss[inst->id].x;
                    ++num;
                }
            assert(num != 0);
            group.y /= num;
            group.x /= num;
        }
        groups.push_back(move(group));
    }

    for (auto inst : database.instances) {
        if (inst->IsDSP() || inst->IsIO()) {
            Group group;
            group.instances.push_back(inst);
            group.id = groups.size();
            if (inst->fixed) {
                // just for safety, the locs will be updated by GP
                auto site = inst->pack->site;
                group.x = site->cx();
                group.y = site->cy();
                // group.y = inst->IsIO() ? database.getIOY(inst) : site->cy();
            } else {
                group.x = instPoss[inst->id].x;
                group.y = instPoss[inst->id].y;
            }
            groups.push_back(group);
        }
    }
    cout << "packble over!" << endl;
}
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

    // ����������Ĳ��ֽ���󶨵�instposs�Ӷ���ʼ��
    instPoss = vector<Point2d>(numInst);
    for (unsigned int i = 0; i < numInst; ++i) {
        instPoss[i].x = groups[i].x;
        instPoss[i].y = groups[i].y;
        // instPoss[i].z = groups[i].z;
    }
    groups.clear();

    // ��������instance��¼lut��dff�ĸ���ͬʱ��instType��instance�����ͺ����±��
    numLUT = 0;
    numFF = 0;
    instType = vector<InstType>(numInst, OTHERS);
    for (unsigned int i = 0; i < numInst; ++i) {
        auto name = database.instances[i]->master->resource->name;
        if (name == Resource::LUT6) {
            ++numLUT;
            int value = database.instances[i]->master->name - Master::LUT1;
            if (value < 7) {
                // ��ripple��һ�������lutOPinIdx��lut1-6�ǲ�һ����...ͦ�����
                database.instances[i]->lutOPinIdx = database.instances[i]->master->pins.size()-1;
                if (database.instances[i]->master->name == Master::LUT6X) value--;
                instType[i] = InstType(value + 1);
            } else { //�������ı���rama������OTHERS
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
    /*����ÿ���������ڷ�����FF��ֻ����dataPin)�������е�pins��
        ������ǵ�degreeֵ�Ա����ǿ�����ͨ��һ�������ble*/ 
    // int iteranum = 246; //����xx�������
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
    // ʵ������ͳ�ƣ�û������
    vector<int> numFF;
    int num2PinNet = 0;
    for (auto inst : database.instances) {
        // cout << inst->name << ',' << inst->pins[inst->lutOPinIdx] << endl;
        if (instType[inst->id] <= LUT6) { //��instanceΪlut
            Net* oNet = inst->pins[inst->lutOPinIdx]->net; //onetΪinstance��outputPin�����ӵ�net
            if (oNet == NULL) {
                continue;
            }
            // cout << oNet->name << ',' << inst->pins[inst->lutOPinIdx]->instance->name << endl;
            unsigned int nFF = 0;
            for (auto pin : oNet->pins) {//����onet��ÿ��pinȻ����ff���͵�instance��pin��Ϊ���ݶ�
                // cout << pin->instance->name << endl;
                if (instType[pin->instance->id] == FF 
                    && pin->type->type == "INPUT" && pin->type->arr=="") {
                    // cout << ":" << Master::NameEnum2String(pin->instance->master->name) << endl;
                    nFF++;
                } 
            }
            if (nFF >= numFF.size()) numFF.resize(nFF + 1, 0);
            ++numFF[nFF];//��¼net��lut��Ϊ������ӵ�dff���������
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
    // ��lut��Χ��instance���뵽ble��
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
            vector<pair<double, unsigned int>> cands; //��ѡ��
            BLE* newBle;
            if (getBLE(lut->x, lut->y, lut->bankz)!=NULL) {// ��ֹ�ظ�new ble
                // ��������lut��ble����
                newBle = inst2ble[database.instances[i]->id];
            } else {
                newBle = NewBle(); // ��newһ��bleȻ�����lut
                // AddLUT(newBle, lut);
                newBle->x = lut->x;
                newBle->y = lut->y;
                newBle->bank = lut->bankz;
                setBLE(newBle->x, newBle->y, newBle->bank, newBle);
                SiteMapBle(lut->pack,newBle,lut->bankz,false); // ��lut��Χ��instance�����ble��
            }
            // cout << newBle->LUTs_[0]->name << endl;            
            if (with2FF[i]==true) continue; //�����Χ�Ѿ�����8��ff�Ͳ����ж��� ֱ����һ��
            // cout << ':' << lut->name << ',' << with2FF[i]<< endl;
            if (lut->pins[lut->lutOPinIdx]->net == NULL) continue;
            for (auto pin : lut->pins[lut->lutOPinIdx]->net->pins) {
                unsigned int idFF = pin->instance->id;
                if (instType[idFF] == FF && pin->type->type == "INPUT" && pin->type->arr=="") {
                    // ��Ϊ���˲�����������seq���ᱻfixed
                    // cout << pin->instance->name << ":" << Master::NameEnum2String(pin->instance->master->name) << endl;
                    /*��lut��x/2+y����<20�ķ�Χѡ����ѡff����ʵ�����ټӸ��ж��Ƿ���ͬһ��clk region*/ 
                    if (inst2ble[idFF] != NULL && 
                        isRealpairLUT(inst2ble[idFF], database.instances[idFF])) continue;
                    double dist = 
                        abs(instPoss[idFF].x - instPoss[lut->id].x) / 2 
                            + abs(instPoss[idFF].y - instPoss[lut->id].y)
                                + abs(instPoss[idFF].z)+ 0.1;
                    // cout << "lut" << lut->name << "," << pin->instance->name << ":" << dist << endl;
                    // if (dist <= mergeRange) // ������Ѿ���ͬһ��site�Ļ��Ͳ����� ���Դ���1
                    cands.push_back(make_pair(dist, idFF));
                    // else
                    //     ++numTooFarFF;
                }
            }
            if (cands.size() != 0) {
                ++numInciLUT; //��¼�ܹ��󶨵�lut��ͳ�Ƶ�ûɶ��
                sort(cands.begin(), cands.end());  // TODO: num of FF, sum of disp? ���ݾ�������
                Instance* ff0 = NULL;
                unsigned char c = 0;
                if (newBle->FFs_.size() == 0) {
                    // ��ble�е�dffΪ�յ����
                    for (;c < cands.size(); c++) {
                        //�������dffѡ����뵽lut���ڵ�site��
                        if (database.canMoveInstance(lut->pack,database.instances[cands[c].second],-1,lut->bankz)) {
                            //����������������src��ce�ж��������жϵ�
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
                    // ��ble��dff��������һ��
                    ff0 = newBle->FFs_[0];
                }
                assert(ff0!=NULL);
                for (; c < cands.size(); ++c) {// ѡ������dff����
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
                                    // ȥ��ԭble�е�ff
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
                                        // ȥ��ԭble�е�ff
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
                            if (newBle->FFs_.size()==8) {//��ʾ���lut�����ɵ�ble�Ѿ���8��dff��
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
                // ����˵����������Ϻ������lut����û���ҵ�������Ե�ff����Ҫ����취�ѵ�ǰ��lut�ƶ�������ble��
                if (isRealpairFF(newBle, lut) == false && lut->fixed == false) {
                    for (int i = 0; i < cands.size(); i++) {
                        Instance* ff2 = database.instances[cands[i].second];
                        if (inst2ble[ff2->id]!=NULL && isRealpairLUT(inst2ble[ff2->id],ff2)) continue;
                        if (inst2ble[ff2->id]!=NULL && inst2ble[ff2->id]->LUTs_.size() == 8) continue;
                        if (database.canMoveInstance(ff2->pack,lut,-1,ff2->bankz)) {
                            newBle->Remove(lut);
                            // SiteMapBle(lut->pack,newBle,lut->bankz,true); ��ο��Բ��ã���Ϊlut��������no pair��
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
    if (lut->bankz == 1) { // ����bank1��lut������z�����������bank0�ģ�����Ҫ��ȥ8
        blepackz -= 8; 
    }
    for (auto pairdff : ble->realPairDff[blepackz]) 
        if (pairdff == true) return false;
    return true;
}
bool PackBLE::isRealpairLUT(BLE* ble, Instance* ff) {
    int blepackz = ff->packz;
    if (ff->bankz == 1) { // ����bank1��lut������z�����������bank0�ģ�����Ҫ��ȥ16
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
    // is valid connected pair �ⲽ���������໥��Ե�lut
    unsigned int numValidConnPair = 0;
    unsigned int numPackedLUT = 0;
    vector<unsigned int> countVCP;
    for (unsigned int i = 0; i < numInst; ++i) {  // instance i ����lut1-5 ��Ϊ�������ֻռ�ð��lut6
        if (instType[i] > LUT5 || withNearLut(database.instances[i]) == -1) continue; //case_1 ��inst_1185��outputΪ��������
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
        for (auto neigh : table1StepConn[i]) {  // instance j �������Ѱ����i��Pin������net�е�lut1-5
            auto j = neigh.first;
            // cout << database.instances[j]->name << endl;
            // if ((inst2ble[j])==0) continue; // ����Ϊ�յ����ţ���Ϊ�����������output
            if (instType[j] > LUT5 || j < i)
                continue;               // ��֮��Ե�lut������lut1-5����û������������ble�е�dff�����ӹ�ϵ
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
                auto conn1 = neigh.second;  // num of 1-step conn1���ǹ�ͬ���ӵ�����������
                /*totΪ����������������������ģ�Ȼ���ȥ�����Ĳ��ܳ���lut6����������*/
                auto tot = database.instances[i]->pins.size() + database.instances[j]->pins.size();
                if ((tot - conn1) > 8) continue;  // fast pruning for constraint 2 �������ǰ���������������Ƿ񳬹�8��(����6���2)
                auto numDupInputs = database.NumDupInputs(*database.instances[i], *database.instances[j]);  // reduce routing demand
                auto numDistInputs = tot - 2 - numDupInputs; //Ϊɶ����con1 ��Ϊcon1������sr��clk����
                // cout << "inst_" << i << ':' ;
                // cout << "inst_" << j << ':' ;
                // cout << "tot��" << tot << ',' << "conn1:" << conn1 << ',' << "numDupInputs:" << numDupInputs << ',' << "numDistInputs:" << numDistInputs << endl;
                if (numDistInputs <= 6) {  // constraint 2: �ϲ������������Ӧ��С�ڵ���6
                    double dist = abs(instPoss[i].x - instPoss[j].x) / 2 + abs(instPoss[i].y - instPoss[j].y);
                    if (numDupInputs > 1 && dist < mergeRange) {  // constraint 3&4: shared inputs, distance
                        int movesitei = withNearLut(database.instances[i]);
                        int movesitej = withNearLut(database.instances[j]);
                        if (isRealpairFF(ble, database.instances[j]) && !isRealpairFF(ble2, database.instances[i])) {
                            // ���j�ܹ����������ڵ�ble��dff��ԣ���i���ܣ�
                            if (database.canMoveInstance(database.instances[j]->pack,database.instances[i], movesitej,database.instances[j]->bankz)) {
                                AddLUT(ble2, database.instances[i]);
                                ble->Remove(database.instances[i]);
                                ++numPackedLUT;
                                SiteMapBle(database.instances[j]->pack,ble,database.instances[j]->bankz,true);
                                break;
                            }
                        } else {
                            // ���iֻռ��һ��z�����ܹ����������ڵ�ble��dff��ԣ���j����;���߶��ܹ�ƥ�䣬��ô�Ͱ�j�ŵ�i���ڵ�site��
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
                // ���԰ѵ�ǰ��lut�ŵ�ble������λ��
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
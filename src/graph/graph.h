
#include "..\include\global.h"
#ifndef _GRAPH_H_
#define _GRAPH_H_
struct Point
{
    double x,y;
    Point() : y(0), x(0) {}
};
struct Edge {
    int v, u, w;
    Edge() {}
    Edge(const int &_v, const int &_u, const int &_w) : v(_v), u(_u), w(_w) {}
};
class Graph
{
public:
    void AddEdge(int u, int v, int w);
    // Graph(/* args */);
    // Graph();

    vector<vector<Edge>> mat;

};

#endif
// OpenBFME. GPL-3.0. Adapted from BFME2Lua wall_navigation_regions.hpp.
#pragma once
#include <algorithm>
#include <cstdint>
#include <vector>
// Read-only connected regions of the engine's ground-navigation cells. Units
// are not obstacles here. Diagonal steps cannot cut through blocked corners.
namespace WallNavigationRegions {
struct Regions {
    int width=0,height=0;unsigned exterior=0;
    std::vector<unsigned> labels,sizes;
    std::vector<unsigned char> outside;
    std::vector<unsigned> borderEdges,wallEdges;
    void build(int w,int h,const std::vector<unsigned char>& passable,const std::vector<unsigned char>& withoutWalls={},
        const std::vector<unsigned char>& wallCells={}){
        width=w;height=h;exterior=0;labels.assign(w*h,0);sizes.assign(1,0);
        std::vector<unsigned> queue;queue.reserve(passable.size());
        for(unsigned start=0;start<passable.size();++start){
            if(!passable[start]||labels[start])continue;
            unsigned label=sizes.size();sizes.push_back(0);queue.clear();queue.push_back(start);labels[start]=label;
            for(size_t at=0;at<queue.size();++at){
                unsigned cell=queue[at];int x=cell%w,y=cell/w;
                for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx){
                    int nx=x+dx,ny=y+dy;if((!dx&&!dy)||nx<0||ny<0||nx>=w||ny>=h)continue;
                    unsigned next=ny*w+nx;if(!passable[next]||labels[next])continue;
                    if(dx&&dy&&(!passable[y*w+nx]||!passable[ny*w+x]))continue;
                    labels[next]=label;queue.push_back(next);
                }
            }
            sizes[label]=queue.size();if(sizes[label]>sizes[exterior])exterior=label;
        }
        outside.assign(sizes.size(),0);
        if(withoutWalls.size()==passable.size()){
            // Each naturally disconnected landmass has its own exterior. Only
            // partitions introduced by walls can produce enclosed rooms.
            Regions natural;natural.build(w,h,withoutWalls);
            std::vector<unsigned> parent(sizes.size(),0),largest(natural.sizes.size(),0);
            for(size_t i=0;i<labels.size();++i)if(labels[i]&&natural.labels[i])parent[labels[i]]=natural.labels[i];
            for(unsigned label=1;label<sizes.size();++label){
                auto p=parent[label];if(p&&sizes[label]>sizes[largest[p]])largest[p]=label;
            }
            for(unsigned label=1;label<sizes.size();++label)
                outside[label]=!parent[label]||largest[parent[label]]==label;
        }else if(exterior)outside[exterior]=1;
        borderEdges.assign(sizes.size(),0);wallEdges.assign(sizes.size(),0);
        if(wallCells.size()==passable.size()){
            // Count exposed cell edges, not neighboring cells or wall objects:
            // a long cliff contributes its full length to the denominator.
            for(int y=0;y<h;++y)for(int x=0;x<w;++x){
                unsigned label=labels[y*w+x];if(!label)continue;
                for(auto delta:{-w,-1,1,w}){
                    bool edge=(delta==-w&&y==0)||(delta==w&&y==h-1)||(delta==-1&&x==0)||(delta==1&&x==w-1);
                    int neighbor=y*w+x+delta;
                    if(edge||labels[neighbor]!=label){
                        ++borderEdges[label];
                        if(!edge&&!passable[neighbor]&&wallCells[neighbor])++wallEdges[label];
                    }
                }
            }
        }
    }
    bool enclosed(int x,int y)const{
        if(x<0||y<0||x>=width||y>=height)return false;
        unsigned label=labels[y*width+x];
        // Building eligibility uses topology only, independently of paving.
        return label&&!outside[label];
    }
    bool pavingEnclosed(int x,int y)const{
        if(!enclosed(x,y))return false;
        unsigned label=labels[y*width+x];
        return borderEdges[label]&&uint64_t(wallEdges[label])*2>=borderEdges[label];
    }
};
}

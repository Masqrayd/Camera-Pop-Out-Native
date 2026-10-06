#include "../src/crop.hpp"
#include <cassert>
#include <iostream>
bool near(double a,double b){return std::abs(a-b)<1e-8;}
int main(){
 auto full=camera::crop(1920,1080,960,540,1,.5,.5);
 assert(near(full.width,1920)&&near(full.height,1080)&&near(full.left,0));
 auto square=camera::crop(1920,1080,600,600,1,.5,.5);
 assert(near(square.width,1080)&&near(square.left,420)&&near(square.height,1080));
 auto zoomed=camera::crop(1920,1080,600,600,2,.5,.5);
 assert(near(zoomed.width,540)&&near(zoomed.height,540));
 auto edge=camera::crop(1920,1080,300,900,3,1,0);
 assert(edge.left+edge.width<=1920+1e-8 && near(edge.top,0));
 assert(near(edge.width/edge.height,300./900.));
 for(int v=1;v<=8;++v)for(int x=0;x<=10;++x)for(int y=0;y<=10;++y){
  auto c=camera::crop(1920,1080,350,800,v,x/10.,y/10.);
  assert(c.left>=0&&c.top>=0&&c.left+c.width<=1920+1e-8&&c.top+c.height<=1080+1e-8);
  assert(near(c.width/c.height,350./800.));
 }
 assert(camera::crop(0,0,600,600,1,.5,.5).width==0);
 std::cout<<"Passed crop coverage, aspect preservation, zoom, pan bounds, and zero dimensions\n";
}

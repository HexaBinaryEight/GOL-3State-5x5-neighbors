#define GRIDWIDTH 850
#define GRIDHEIGHT 650

/*
Conway's Game of Life, 3 states 5x5 neighbors

Cells are either -1, 0, or +1, and every frame
a cell gets the sum of its surrounding 24 neighbors (a 5x5 region around the cell)
Then, on a 3x49 rule chart, that cell's next state is [current state]x[sum]

the exact implementation might seem confusing though.
The naive approach is to just do something like...

    for(int x=0;x<width;x++)
    {
      for(int y=0;y<height;y++)
      {
          gridnext[x][y]=rules[gridnow[x][y]][sum(x,y)];
      }
    }

    for(int x=0;x<width;x++)
    {
      for(int y=0;y<height;y++)
      {
          gridnow[x][y]=gridnext[x][y];
      }
    }

Which is fine, sure. It technically works, but it's very slow.
My first step was to remove the copy phase (gridnext to gridnow after each frame)
In short, we don't need to copy, but instead alternate
which grid is "now" and which is "next", each frame.

The next step is a bit more convoluted but conceptually simple:
We only process cells whose neighbors have changed states.
A cell can only change if its neighbors have changed.

(3+3 is always 6, so if the inputs don't change, the output certainly didn't change!)

This is done by filling a list with cell IDs (the grids are technically 1D arrays)
Initially, the list is populated with ALL cells, so the startup is slow.
We then iterate through the list of cell IDs and process them.
When a cell is processed, it is removed from the list,
and IF the cell has changed states, it will add (if they aren't already there)
all 24 of its neighbors to the list for the next frame.

TL;DR low entropy systems run way faster than they would in the naive approach.

the mcellgrid class contains a spaghettified mess of pointers and functions,
sorry about that. I kinda whipped this up on a whim. :P

Most the functions are just for things like converting a cell ID to x or y coordinate
or vice-versa, and wrapping xy around.

I also didn't have time to setup a proper GUI, since I'm still learning to use SFML.

Oh well.

None the less, enjoy. <3

-HexaBinary
12:05 PM EST, 25 June 2017

--------

Something of interest to note: When processing cells, if a cell has changed,
its neighbors are to be added to the list of cells to process next frame. Makes sense.
What I don't understand is why I also need to add the changed cell to that
list aswell... not doing so yields pixel artifacts; cells that take on a state and will
never change (they actually alternate between 2 states), but will affect other cells.

-HexaBinary
11:47 PM EST, 25 June 2017

--------
*/

#include <SFML/Graphics.hpp>
#include <SFML/Window.hpp>
#include <SFML/System.hpp>
#include "MersenneTwister.h"

MTRand_int32 random;

struct coord{int x,y;};
class mcellgrid
{
  #define RULEPADSIZE 12
  #define RULEPADWIDTH 36
  #define RULEPADHEIGHT 588

  //width should be rule pad (cell) size * 3

  public:
  int width,height,area,zoom;
  sf::RenderTexture surface;
  sf::RenderTexture rulepad;
  sf::RectangleShape pos,neu,neg; //3 because I suspect that changing their color requires sending data to the GPU... and since we only want 3 colors, send them all at start.
 //Maybe in the future I can learn about shaders to make it look really nice... somehow.
 sf::RectangleShape error; //just in case the grid takes on invalid values...

  //Current == a on frame 0. So even==a, odd==b.
  //cell values are 0==negative, 1==neutral, 2==positive
  int* grida;
  int* gridb;
  int* gridcurrent; //the current state of the cells
  int* gridnext; //what they're gonna be when they grow up! (These are actually pointers to grida and gridb)
  bool* affected0, *affected1; //Keep track of which cells have been affected by changing neighbors
  int* toprocess0, *toprocess1; //List of cells to process. Length is stored at toprocess_[area].
  bool* affectedcurrent, *affectednext;//pointers to the affected lists...
  int* processcurrent, *processnext;//pointers to the toprocess lists...
  int rules[3][49];
  int frame;

  int init(int w, int h)
  {
    width=w;
    height=h;
    area=w*h;

    if(!surface.create(width,height))
    {return -1;}
    if(!rulepad.create(RULEPADWIDTH,RULEPADHEIGHT))
    {return -2;}

    surface.clear(sf::Color(0,0,200));
    surface.setRepeated(true);

    grida=new int[area];
    gridb=new int[area];
    affected0=new bool[area];
    affected1=new bool[area];
    toprocess0=new int[area+1];//the last entry is how many cells need processing.
    toprocess1=new int[area+1]; //entries before that are the IDs of cells to process.

    affectedcurrent=affected0;
    affectednext=affected1;

    toprocess0[area]=area;
    toprocess1[area]=0;

    processcurrent=toprocess0;
    processnext=toprocess1;

    gridcurrent=grida;
    gridnext=gridb;

    for(int i=0;i<area;i++)
    {
      grida[i]=gridb[i]=1;
      affectedcurrent[i]=true;
      affectednext[i]=false;
      processcurrent[i]=i;
      processnext[i]=0;
    }

    for(int x=0;x<3;x++)
    {
      for(int y=0;y<49;y++)
      {
        rules[x][y]=1;
      }
    }
    frame=0;
    zoom=1;
    pos.setSize(sf::Vector2f(1.0,1.0));
    neu.setSize(sf::Vector2f(1.0,1.0));
    neg.setSize(sf::Vector2f(1.0,1.0));
    error.setSize(sf::Vector2f(1.0,1.0));

    pos.setFillColor(sf::Color(255,245,235));
    neu.setFillColor(sf::Color(137,127,117));
    neg.setFillColor(sf::Color(15,10,5));
    error.setFillColor(sf::Color(255,0,0));

    neg.setOutlineColor(sf::Color(0,0,63));
    neu.setOutlineColor(sf::Color(0,0,63));
    pos.setOutlineColor(sf::Color(0,0,63));
    error.setOutlineColor(sf::Color(127,0,0));
    renderrules();
    return 1;
  }

  int xytoi(int x, int y)
  {
    return (x%width)+(y*width);
  }

  int itox(int i)
  {
    return i%width;
  }

  int itoy(int i)
  {
    return i/width;
  }

  coord itoxy(int i)
  {
    coord r;
    r.x=itox(i);
    r.y=itoy(i);
    return r;
  }

  int wrapx(int x)
  {
    while(x<0){x+=width;}
    while(x>=width){x-=width;}
    return x;
  }

  int wrapy(int y)
  {
    while(y<0){y+=height;}
    while(y>=height){y-=height;}
    return y;
  }

  int wrapi(int i)
  {
    int x,y;
    x=wrapx(itox(i)); //<- Actually, this wrapx shouldn't be necessary. But we'll keep it for future proofing.
    y=wrapy(itoy(i));
    return xytoi(x,y);
  }

  int getcell(int x, int y)
  {
    x=wrapx(x);
    y=wrapy(y);
    return gridcurrent[xytoi(x,y)];
  }

  int getsum(int x, int y)
  {
    int s=0;
    for(int xx=x-2;xx<=x+2;xx++)
    {
      for(int yy=y-2;yy<=y+2;yy++)
      {
        if(xx==x && yy==y){continue;}
        s+=getcell(wrapx(xx),wrapy(yy));
      }
    }
    return s;
  }

  void affect(int x, int y)
  {
    int i=xytoi(wrapx(x),wrapy(y));
    if(affectednext[i]==false)
    {
      affectednext[i]=true;
      processnext[processnext[area]]=i;
      processnext[area]++;
    }
  }

  void affectregion(int x, int y)
  {//Affects all neighbors of x,y
    for(int xx=x-2;xx<=x+2;xx++)
    {
      for(int yy=y-2;yy<=y+2;yy++)
      {
        //if(xx==x&&yy==y){continue;}
        affect(xx,yy);
      }
    }
  }

  void setcell(int x, int y, int c)
  {
    int i=xytoi(x,y);
    gridnext[i]=c;
    affectedcurrent[i]=false;
    if(c!=gridcurrent[i])
    {
      //set surrounding cells as being affected.
      affectregion(x,y);
    }
  }

  void setcellrender(int x, int y, int c)
  {
    int i=xytoi(x,y);
    gridnext[i]=c;
    affectedcurrent[i]=false;
    if(c!=gridcurrent[i])
    {
      //set surrounding cells as being affected.
      affectregion(x,y);

      //If the cell hasn't changed, we don't need to redraw anything, do we? Nope!
      rendercell(x,y,c);
    }
  }

  int getrule(int type, int sum)
  {
    return rules[type][sum];
  }

  void step()
  {
    for(int i=0;i<processcurrent[area];i++)
    {
      int x=itox(processcurrent[i]);
      int y=itoy(processcurrent[i]);
      setcell(x,y,getrule(gridcurrent[processcurrent[i]],getsum(x,y)));
    }
    processcurrent[area]=0;

    frame++;
    if( (frame&1)==0 )
    {
      gridcurrent=grida;
      gridnext=gridb;
      processcurrent=toprocess0;
      processnext=toprocess1;
      affectedcurrent=affected0;
      affectednext=affected1;
    }
    else
    {
      gridcurrent=gridb;
      gridnext=grida;
      processcurrent=toprocess1;
      processnext=toprocess0;
      affectedcurrent=affected1;
      affectednext=affected0;
    }
  }

  void steprender()
  {
    for(int i=0;i<processcurrent[area];i++)
    {
      int x=itox(processcurrent[i]);
      int y=itoy(processcurrent[i]);
      setcellrender(x,y,getrule(gridcurrent[processcurrent[i]],getsum(x,y)));
    }
    processcurrent[area]=0;

    frame++;
    if( (frame&1)==0 )
    {
      gridcurrent=grida;
      gridnext=gridb;
      processcurrent=toprocess0;
      processnext=toprocess1;
      affectedcurrent=affected0;
      affectednext=affected1;
    }
    else
    {
      gridcurrent=gridb;
      gridnext=grida;
      processcurrent=toprocess1;
      processnext=toprocess0;
      affectedcurrent=affected1;
      affectednext=affected0;
    }
  }

  void stepnaive()
  {
    for(int x=0;x<width;x++)
    {
      for(int y=0;y<height;y++)
      {
        gridnext[xytoi(x,y)] = getrule(gridcurrent[xytoi(x,y)],getsum(wrapx(x),wrapy(y)));
      }
    }

    int* temp;
    temp=gridcurrent;
    gridcurrent=gridnext;
    gridnext=temp;
    return;

    for(int x=0;x<width;x++)
    {
      for(int y=0;y<height;y++)
      {
        gridcurrent[xytoi(x,y)]=gridnext[xytoi(x,y)];
      }
    }
  }

  void renderrules()
  {
    neg.setSize(sf::Vector2f(12,12));
    neu.setSize(sf::Vector2f(12,12));
    pos.setSize(sf::Vector2f(12,12));

    neg.setOutlineThickness(1.0);
    neu.setOutlineThickness(1.0);
    pos.setOutlineThickness(1.0);

    sf::RectangleShape *rs;

    for(int type=0;type<3;type++)
    {
      for(int sum=0;sum<49;sum++)
      {
        int gr=getrule(type,sum);
        switch(gr)
        {
        case 0:
          rs=&neg;
          break;
        case 1:
          rs=&neu;
          break;
        default:
          rs=&pos;
          break;
        }
        (*rs).setPosition(type*12,(48*12)-sum*12);
        rulepad.draw((*rs));
      }
    }

    neg.setSize(sf::Vector2f(1,1));
    neu.setSize(sf::Vector2f(1,1));
    pos.setSize(sf::Vector2f(1,1));

    neg.setOutlineThickness(0.0);
    neu.setOutlineThickness(0.0);
    pos.setOutlineThickness(0.0);
  }

  void rendercell(int x, int y,int c)
  {
    switch(c)
    {
      case 0: //negative
      {
        neg.setPosition(x,y);
        surface.draw(neg);
        break;
      }
      case 1: //neutral
      {
        neu.setPosition(x,y);
        surface.draw(neu);
        break;
      }
      case 2: //positive
      {
        pos.setPosition(x,y);
        surface.draw(pos);
        break;
      }
      default: //Error
      {//<Insert some philosophical comment about how the default case is error>
        error.setPosition(x,y);
        surface.draw(error);
        break;
      }
    }
  }

  void rendergrid()
  {
    for(int i=0;i<area;i++)
    {
      int x=itox(i);
      int y=itoy(i);
      rendercell(x,y,getcell(x,y));
    }
  }

  void clickrules(int x, int y, int mb)
  {//x,y should be relative to the top left corner of the rule pad! so x=0 is the left of the rule pad.
    //mb is the mouse button used (1==left, 2==right)
    int type,sum;
    type=x/12;
    sum=y/12;
    if(mb==1)
    {
      rules[type][sum]+=1;
      if(rules[type][sum]>2)
      {
        rules[type][sum]=0;
      }
    }
    else if(mb==2)
    {
      rules[type][sum]-=1;
      if(rules[type][sum]<0)
      {
        rules[type][sum]=2;
      }
    }
    renderrules();
  }

  void experiment(int p)
  {
    switch(p)
    {
      case 0:
      {
        rules[0][0]=2;
        rules[2][48]=0;
        break;
      }
      case 1:
      {
        rules[1][24]=2;
        rules[2][48]=1;
        rules[2][47]=0;
        rules[2][46]=2;
        rules[1][48]=0;
        rules[1][47]=0;
        rules[1][46]=0;
        rules[1][45]=0;
        for(int x=0;x<width;x++)
        {
          for(int y=0;y<height;y++)
          {
            gridcurrent[xytoi(x,y)]=(x&y)%3;
          }
        }
        break;
      }
      case 2:
      {

        for(int i=0;i<2;i++)
        {
          for(int j=0;j<49;j++)
          {
            rules[i][j]=random()%3;
          }
        }
        for(int i=0;i<area;i++)
        {
          gridcurrent[i]=random()%3;
        }
      }
      case 3:
      {
        for(int t=0;t<3;t++)
        {
          for(int s=0;s<49;s++)
          {
            rules[t][s]=1;
          }
        }
        for(int i=48;i>=31;i--)
        {
          rules[2][i]=2;
        }
        rules[2][30]=0;
        rules[2][28]=2;
        rules[1][28]=2;
        rules[2][27]=0;

        for(int i=0;i<area;i++)
        {
          gridcurrent[i]=random()%3;
          gridnext[i]=gridcurrent[i];
        }
        rendergrid();
      }
    }
  }
};

void dotmatrixdot(int x, int y,sf::RenderWindow* w)
{
  sf::RectangleShape d;
  d.setSize(sf::Vector2f(4.0,4.0));
  d.setFillColor(sf::Color(0,255,0));
  d.setOutlineColor(sf::Color(0,127,0));
  d.setOutlineThickness(1.0);

  d.setPosition(x*5,y*5);
  w->draw(d);
}

class camera
{
  public:
  int x,y,w,h,z,ac;//position, size, zoom, acceleration.
  bool moved;
  void init()
  {
    moved=false;
    x=y=0;
    w=h=0;
    z=1;
    ac=1;
  }

  int a()
  {
    moved=true;
    ac++;
    if(ac>64){ac=64;}
    return ac;
  }

  void checkmoved()
  {
    if(moved)
    {
      moved=false;
    }
    else
    {
      ac=ac/2;
      if(ac<1){ac=1;}
    }
  }
};

int main()
{
  sf::RenderWindow window(sf::VideoMode(800,600),"CGoL 3 state cells 5x5");
  random.seed(99);

  {
    window.clear(sf::Color(0,0,0));
    int x[]=
    {
      0,0 , 0,1 , 0,2 , 1,2 , 2,2, //L
      4,1 , 5,0 , 6,1 , 5,2, //O
      8,2 , 9,1 , 10,0 , 11,1 , 12,2, //A
      14,0 , 14,1 , 14,2 , 15,0 , 16,1 , 15,2 //D
      };
    for(int i=0;i<40;i+=2)
    {
      dotmatrixdot(x[i]+10,x[i+1]+40,&window);
    }
    window.display();
  }

    int framerate=1;
    int framerateoptions[]={30,45,60,90,0,1,2,4,8,16};
    window.setFramerateLimit(framerateoptions[framerate]);

  mcellgrid grid;
  if(grid.init(GRIDWIDTH,GRIDHEIGHT)!=1)
  {
    return -1;
  }
  grid.experiment(3);
  grid.renderrules();

  int mousex, mousey;
  mousex=0;
  mousey=0;
  camera cam;
  cam.init();

  cam.w=(window.getSize().x-RULEPADWIDTH)-8;
  cam.h=600;
  bool autorender=true;

  bool RUN=true;
  sf::Event e;
  while(RUN)
  {
    if(window.isOpen())
    {
      window.clear(sf::Color(75,125,100));

      while(window.pollEvent(e))
      {
        if(e.type==sf::Event::Closed)
        {
          RUN=false;
        }
        else if(e.type==sf::Event::KeyPressed)
        {
          if(e.key.code==sf::Keyboard::Escape)
          {
            RUN=false;
          }
          else if(e.key.code==sf::Keyboard::S)
          {
            grid.rendergrid();
            grid.surface.getTexture().copyToImage().saveToFile("Metacell_Output.png");
          }
          else if(e.key.code==sf::Keyboard::F)
          {//toggle framerate.
            framerate++;
            if(framerate>9){framerate=0;}
            window.setFramerateLimit(framerateoptions[framerate]);
          }
          else if(e.key.code==sf::Keyboard::Z)
          {
            if(cam.z==1)
            {
              cam.z=8;
            }
            else
            {
              cam.z=1;
            }
          }
          else if(e.key.code==sf::Keyboard::R)
          {
            autorender=!autorender;
            if(autorender){grid.rendergrid();}
          }
        }

        else if(e.type==sf::Event::MouseMoved)
        {
          mousex=e.mouseMove.x;
          mousey=e.mouseMove.y;
        }

        else if(e.type==sf::Event::MouseButtonPressed)
        {
          //Check if they clicked the rulepad:
          int w;
          w=window.getSize().x;
          if(mousex>=w-(3*12) && mousey>=0 && mousex<=w && mousey<=49*12)
          {
            if(e.mouseButton.button==sf::Mouse::Left)
            {
              grid.clickrules(mousex-(w-(3*12)),mousey,1);
            }
            else if(e.mouseButton.button==sf::Mouse::Right)
            {
              grid.clickrules(mousex-(w-(3*12)),mousey,2);
            }
          }
        }
      }

      //Move the camera around. Also zoom and stuff.
      if(sf::Keyboard::isKeyPressed(sf::Keyboard::Left))
      {cam.x-=cam.a();}
      else if(sf::Keyboard::isKeyPressed(sf::Keyboard::Right))
      {cam.x+=cam.a();}
      if(sf::Keyboard::isKeyPressed(sf::Keyboard::Up))
      {cam.y-=cam.a();}
      else if(sf::Keyboard::isKeyPressed(sf::Keyboard::Down))
      {cam.y+=cam.a();}
      cam.checkmoved();


      autorender?grid.steprender():grid.step();


      sf::Sprite sp;
      sp.setTexture(grid.surface.getTexture());
      sp.setTextureRect(sf::IntRect(cam.x,cam.y,cam.w,cam.h));
      sp.setScale(cam.z,cam.z);
      window.draw(sp);

      sf::Sprite spr;
      spr.setTexture(grid.rulepad.getTexture());
      spr.setPosition(window.getSize().x-(3*12),0.0);
      spr.setScale(1.0,1.0);
      window.draw(spr);

      window.display();

    }
  }

  window.close();
  return 0;
}

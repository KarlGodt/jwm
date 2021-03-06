/**
 * @file taskbar.c
 * @author Joe Wingbermuehle
 * @date 2005-2006
 *
 * @brief Task list tray component.
 *
 */

#include "jwm.h"
#include "taskbar.h"
#include "tray.h"
#include "timing.h"
#include "main.h"
#include "client.h"
#include "clientlist.h"
#include "color.h"
#include "popup.h"
#include "button.h"
#include "cursor.h"
#include "icon.h"
#include "error.h"
#include "winmenu.h"
#include "screen.h"
#include "settings.h"
#include "event.h"

typedef struct TaskBarType {

   TrayComponentType *cp;

   int itemHeight;
   LayoutType layout;

   Pixmap buffer;

   TimeType mouseTime;
   int mousex, mousey;

   unsigned int maxItemWidth;

   struct TaskBarType *next;

} TaskBarType;

typedef struct Node {
   ClientNode *client;
   int y;
   struct Node *next;
   struct Node *prev;
} Node;

static TaskBarType *bars;
static Node *taskBarNodes;
static Node *taskBarNodesTail;

static Node *GetNode(TaskBarType *bar, int x);
static unsigned int GetItemCount(void);
static unsigned int GetItemWidth(const TaskBarType *bp,
                                 unsigned int itemCount);
static void Render(const TaskBarType *bp);
static void ShowTaskWindowMenu(TaskBarType *bar, Node *np);

static void SetSize(TrayComponentType *cp, int width, int height);
static void Create(TrayComponentType *cp);
static void Resize(TrayComponentType *cp);
static void ProcessTaskButtonEvent(TrayComponentType *cp,
                                   int x, int y, int mask);
static void ProcessTaskMotionEvent(TrayComponentType *cp,
                                   int x, int y, int mask);
static void SignalTaskbar(const TimeType *now, int x, int y, Window w,
                          void *data);

/** Initialize task bar data. */
void InitializeTaskBar(void)
{
   bars = NULL;
   taskBarNodes = NULL;
   taskBarNodesTail = NULL;
}

/** Shutdown the task bar. */
void ShutdownTaskBar(void)
{
   TaskBarType *bp;
   for(bp = bars; bp; bp = bp->next) {
      JXFreePixmap(display, bp->buffer);
   }
}

/** Destroy task bar data. */
void DestroyTaskBar(void)
{
   TaskBarType *bp;
   while(bars) {
      bp = bars->next;
      UnregisterCallback(SignalTaskbar, bars);
      Release(bars);
      bars = bp;
   }
}

/** Create a new task bar tray component. */
TrayComponentType *CreateTaskBar()
{

   TrayComponentType *cp;
   TaskBarType *tp;

   tp = Allocate(sizeof(TaskBarType));
   tp->next = bars;
   bars = tp;
   tp->itemHeight = 0;
   tp->layout = LAYOUT_HORIZONTAL;
   tp->mousex = -settings.doubleClickDelta;
   tp->mousey = -settings.doubleClickDelta;
   tp->mouseTime.seconds = 0;
   tp->mouseTime.ms = 0;
   tp->maxItemWidth = 0;

   cp = CreateTrayComponent();
   cp->object = tp;
   tp->cp = cp;

   cp->SetSize = SetSize;
   cp->Create = Create;
   cp->Resize = Resize;
   cp->ProcessButtonPress = ProcessTaskButtonEvent;
   cp->ProcessMotionEvent = ProcessTaskMotionEvent;

   RegisterCallback(settings.popupDelay / 2, SignalTaskbar, tp);

   return cp;

}

/** Set the size of a task bar tray component. */
void SetSize(TrayComponentType *cp, int width, int height)
{

   TaskBarType *tp;

   Assert(cp);

   tp = (TaskBarType*)cp->object;

   Assert(tp);

   if(width == 0) {
      tp->layout = LAYOUT_HORIZONTAL;
   } else if(height == 0) {
      tp->layout = LAYOUT_VERTICAL;
   } else if(width > height) {
      tp->layout = LAYOUT_HORIZONTAL;
   } else {
      tp->layout = LAYOUT_VERTICAL;
   }

}

/** Initialize a task bar tray component. */
void Create(TrayComponentType *cp)
{

   TaskBarType *tp;

   Assert(cp);

   tp = (TaskBarType*)cp->object;

   Assert(tp);

   if(tp->layout == LAYOUT_HORIZONTAL) {
      tp->itemHeight = cp->height;
   } else {
      tp->itemHeight = GetStringHeight(FONT_TASK) + 12;
   }

   Assert(cp->width > 0);
   Assert(cp->height > 0);

   cp->pixmap = JXCreatePixmap(display, rootWindow, cp->width, cp->height,
                               rootVisual.depth);
   tp->buffer = cp->pixmap;

   ClearTrayDrawable(cp);

}

/** Resize a task bar tray component. */
void Resize(TrayComponentType *cp)
{

   TaskBarType *tp;

   Assert(cp);

   tp = (TaskBarType*)cp->object;

   Assert(tp);

   if(tp->buffer != None) {
      JXFreePixmap(display, tp->buffer);
   }

   if(tp->layout == LAYOUT_HORIZONTAL) {
      tp->itemHeight = cp->height;
   } else {
      tp->itemHeight = GetStringHeight(FONT_TASK) + 12;
   }

   Assert(cp->width > 0);
   Assert(cp->height > 0);

   cp->pixmap = JXCreatePixmap(display, rootWindow, cp->width, cp->height,
                               rootVisual.depth);
   tp->buffer = cp->pixmap;

   ClearTrayDrawable(cp);
}

/** Process a task list button event. */
void ProcessTaskButtonEvent(TrayComponentType *cp, int x, int y, int mask)
{

   TaskBarType *bar = (TaskBarType*)cp->object;
   Node *np;

   Assert(bar);

   if(bar->layout == LAYOUT_HORIZONTAL) {
      np = GetNode(bar, x);
   } else {
      np = GetNode(bar, y);
   }

   if(np) {
      switch(mask) {
      case Button1:
         if((np->client->state.status & STAT_ACTIVE) &&
            !(np->client->state.status & STAT_MINIMIZED)) {
            MinimizeClient(np->client, 1);
         } else {
            RestoreClient(np->client, 1);
            FocusClient(np->client);
         }
         break;
      case Button3:
         ShowTaskWindowMenu(bar, np);
         break;
      case Button4:
         FocusPrevious();
         break;
      case Button5:
         FocusNext();
         break;
      default:
         break;
      }
   }

}

/** Process a task list motion event. */
void ProcessTaskMotionEvent(TrayComponentType *cp, int x, int y, int mask)
{
   TaskBarType *bp = (TaskBarType*)cp->object;
   bp->mousex = cp->screenx + x;
   bp->mousey = cp->screeny + y;
   GetCurrentTime(&bp->mouseTime);
}

/** Show the menu associated with a task list item. */
void ShowTaskWindowMenu(TaskBarType *bar, Node *np)
{

   int x, y;
   int mwidth, mheight;
   const ScreenType *sp;
   Window w;

   GetWindowMenuSize(np->client, &mwidth, &mheight);

   sp = GetCurrentScreen(bar->cp->screenx, bar->cp->screeny);

   if(bar->layout == LAYOUT_HORIZONTAL) {
      GetMousePosition(&x, &y, &w);
      if(bar->cp->screeny + bar->cp->height / 2 < sp->y + sp->height / 2) {
         y = bar->cp->screeny + bar->cp->height;
      } else {
         y = bar->cp->screeny - mheight;
      }
      x -= mwidth / 2;
   } else {
      if(bar->cp->screenx + bar->cp->width / 2 < sp->x + sp->width / 2) {
         x = bar->cp->screenx + bar->cp->width;
      } else {
         x = bar->cp->screenx - mwidth;
      }
      y = bar->cp->screeny + np->y;
   }

   ShowWindowMenu(np->client, x, y);

}

/** Add a client to the task bar. */
void AddClientToTaskBar(ClientNode *np)
{

   Node *tp;

   Assert(np);

   tp = Allocate(sizeof(Node));
   tp->client = np;

   if(settings.taskInsertMode == INSERT_RIGHT) {
      tp->next = NULL;
      tp->prev = taskBarNodesTail;
      if(taskBarNodesTail) {
         taskBarNodesTail->next = tp;
      } else {
         taskBarNodes = tp;
      }
      taskBarNodesTail = tp;
   } else {
      tp->prev = NULL;
      tp->next = taskBarNodes;
      if(taskBarNodes) {
         taskBarNodes->prev = tp;
      }
      taskBarNodes = tp;
      if(!taskBarNodesTail) {
         taskBarNodesTail = tp;
      }
   }

   UpdateTaskBar();
   UpdateNetClientList();

}

/** Remove a client from the task bar. */
void RemoveClientFromTaskBar(ClientNode *np)
{

   Node *tp;

   Assert(np);

   for(tp = taskBarNodes; tp; tp = tp->next) {
      if(tp->client == np) {
         if(tp->prev) {
            tp->prev->next = tp->next;
         } else {
            taskBarNodes = tp->next;
         }
         if(tp->next) {
            tp->next->prev = tp->prev;
         } else {
            taskBarNodesTail = tp->prev;
         }
         Release(tp);
         break;
      }
   }

   UpdateTaskBar();
   UpdateNetClientList();

}

/** Update all task bars. */
void UpdateTaskBar(void)
{

   TaskBarType *bp;
   int lastHeight = -1;

   if(JUNLIKELY(shouldExit)) {
      return;
   }

   for(bp = bars; bp; bp = bp->next) {
      if(bp->layout == LAYOUT_VERTICAL) {
         lastHeight = bp->cp->requestedHeight;
         bp->cp->requestedHeight = GetStringHeight(FONT_TASK) + 12;
         bp->cp->requestedHeight *= GetItemCount();
         bp->cp->requestedHeight += 2;
         if(lastHeight != bp->cp->requestedHeight) {
            ResizeTray(bp->cp->tray);
         }
      }
      Render(bp);
   }

}

/** Signal task bar (for popups). */
void SignalTaskbar(const TimeType *now, int x, int y, Window w, void *data)
{

   TaskBarType *bp = (TaskBarType*)data;
   Node *np;

   if(w == bp->cp->tray->window &&
      abs(bp->mousex - x) < settings.doubleClickDelta &&
      abs(bp->mousey - y) < settings.doubleClickDelta) {
      if(GetTimeDifference(now, &bp->mouseTime) >= settings.popupDelay) {
         if(bp->layout == LAYOUT_HORIZONTAL) {
            np = GetNode(bp, x - bp->cp->screenx);
         } else {
            np = GetNode(bp, y - bp->cp->screeny);
         }
         if(np && np->client->name) {
            ShowPopup(x, y, np->client->name);
         }
      }
   }

}

/** Draw a specific task bar. */
void Render(const TaskBarType *bp)
{

   Node *tp;
   ButtonNode button;
   int x, y;
   int remainder;
   int itemWidth, itemCount;
   int width;
   Pixmap buffer;
   GC gc;
   char *minimizedName;

   if(JUNLIKELY(shouldExit)) {
      return;
   }

   Assert(bp);
   Assert(bp->cp);

   width = bp->cp->width;
   buffer = bp->cp->pixmap;
   gc = rootGC;

   x = 0;
   width -= x;
   y = 0;

   ClearTrayDrawable(bp->cp);

   itemCount = GetItemCount();
   if(!itemCount) {
      UpdateSpecificTray(bp->cp->tray, bp->cp);
      return;
   }
   if(bp->layout == LAYOUT_HORIZONTAL) {
      itemWidth = GetItemWidth(bp, itemCount);
      remainder = width - itemWidth * itemCount;
   } else {
      itemWidth = width;
      remainder = 0;
   }

   ResetButton(&button, buffer, &rootVisual);
   button.font = FONT_TASK;

   for(tp = taskBarNodes; tp; tp = tp->next) {
      if(ShouldFocus(tp->client)) {

         tp->y = y;

         if(tp->client->state.status & (STAT_ACTIVE | STAT_FLASH)) {
            button.type = BUTTON_TASK_ACTIVE;
         } else {
            button.type = BUTTON_TASK;
         }

         if(remainder) {
            button.width = itemWidth;
         } else {
            button.width = itemWidth - 1;
         }
         button.height = bp->itemHeight;
         button.x = x;
         button.y = y;
         button.icon = tp->client->icon;

         if(tp->client->state.status & STAT_MINIMIZED) {
            if(tp->client->name) {
               minimizedName = AllocateStack(strlen(tp->client->name) + 3);
               sprintf(minimizedName, "[%s]", tp->client->name);
               button.text = minimizedName;
               DrawButton(&button);
               ReleaseStack(minimizedName);
            } else {
               button.text = "[]";
               DrawButton(&button);
            }
         } else {
            button.text = tp->client->name;
            DrawButton(&button);
         }

         if(tp->client->state.status & STAT_MINIMIZED) {
            const int isize = (bp->itemHeight + 7) / 8;
            int i;
            JXSetForeground(display, gc, colors[COLOR_TASK_FG]);
            for(i = 0; i <= isize; i++) {
               const int xc = x + i + 3;
               const int y1 = bp->itemHeight - 3 - isize + i;
               const int y2 = bp->itemHeight - 3;
               JXDrawLine(display, buffer, gc, xc, y1, xc, y2);
            }
         }

         if(bp->layout == LAYOUT_HORIZONTAL) {
            x += itemWidth;
            if(remainder) {
               x += 1;
               remainder -= 1;
            }
         } else {
            y += bp->itemHeight;
            if(remainder) {
               y += 1;
               remainder -= 1;
            }
         }

      }
   }

   UpdateSpecificTray(bp->cp->tray, bp->cp);

}

/** Focus the next client in the task bar. */
void FocusNext(void)
{

   Node *tp;

   for(tp = taskBarNodes; tp; tp = tp->next) {
      if(ShouldFocus(tp->client)) {
         if(tp->client->state.status & STAT_ACTIVE) {
            tp = tp->next;
            break;
         }
      }
   }

   if(!tp) {
      tp = taskBarNodes;
   }

   while(tp && !ShouldFocus(tp->client)) {
      tp = tp->next;
   }

   if(!tp) {
      tp = taskBarNodes;
      while(tp && !ShouldFocus(tp->client)) {
         tp = tp->next;
      }
   }

   if(tp) {
      RestoreClient(tp->client, 1);
      FocusClient(tp->client);
   }

}

/** Focus the previous client in the task bar. */
void FocusPrevious(void)
{

   Node *tp;

   for(tp = taskBarNodesTail; tp; tp = tp->prev) {
      if(ShouldFocus(tp->client)) {
         if(tp->client->state.status & STAT_ACTIVE) {
            tp = tp->prev;
            break;
         }
      }
   }

   if(!tp) {
      tp = taskBarNodesTail;
   }

   while(tp && !ShouldFocus(tp->client)) {
      tp = tp->prev;
   }

   if(!tp) {
      tp = taskBarNodesTail;
      while(tp && !ShouldFocus(tp->client)) {
         tp = tp->prev;
      }
   }

   if(tp) {
      RestoreClient(tp->client, 1);
      FocusClient(tp->client);
   }

}

/** Get the item associated with an x-coordinate on the task bar. */
Node *GetNode(TaskBarType *bar, int x)
{

   Node *tp;
   int index, stop;

   index = 0;
   if(bar->layout == LAYOUT_HORIZONTAL) {

      const int width = bar->cp->width - index; 
      const unsigned int itemCount = GetItemCount();
      const int itemWidth = GetItemWidth(bar, itemCount);
      int remainder = width - itemWidth * itemCount;

      for(tp = taskBarNodes; tp; tp = tp->next) {
         if(ShouldFocus(tp->client)) {
            if(remainder) {
               stop = index + itemWidth + 1;
               remainder -= 1;
            } else {
               stop = index + itemWidth;
            }
            if(x >= index && x < stop) {
               return tp;
            }
            index = stop;
         }
      }

   } else {

      for(tp = taskBarNodes; tp; tp = tp->next) {
         if(ShouldFocus(tp->client)) {
            stop = index + bar->itemHeight;
            if(x >= index && x < stop) {
               return tp;
            }
            index = stop;
         }
      }

   }

   return NULL;

}

/** Get the number of items on the task bar. */
unsigned int GetItemCount(void)
{

   Node *tp;
   unsigned int count;

   count = 0;
   for(tp = taskBarNodes; tp; tp = tp->next) {
      if(ShouldFocus(tp->client)) {
         count += 1;
      }
   }

   return count;

}

/** Get the width of an item in the task bar. */
unsigned int GetItemWidth(const TaskBarType *bp, unsigned int itemCount)
{

   unsigned int itemWidth;

   itemWidth = bp->cp->width;

   if(!itemCount) {
      return itemWidth;
   }

   itemWidth /= itemCount;
   if(!itemWidth) {
      itemWidth = 1;
   }

   if(bp->maxItemWidth > 0 && itemWidth > bp->maxItemWidth) {
      itemWidth = bp->maxItemWidth;
   }

   return itemWidth;

}

/** Set the maximum width of an item in the task bar. */
void SetMaxTaskBarItemWidth(TrayComponentType *cp, const char *value)
{

   TaskBarType *bp;
   int temp;

   Assert(cp);
   Assert(value);

   temp = atoi(value);
   if(JUNLIKELY(temp < 0)) {
      Warning(_("invalid maxwidth for TaskList: %s"), value);
      return;
   }
   bp = (TaskBarType*)cp->object;
   bp->maxItemWidth = temp;

}

/** Maintain the _NET_CLIENT_LIST[_STACKING] properties on the root. */
void UpdateNetClientList(void)
{

   Node *np;
   ClientNode *client;
   Window *windows;
   unsigned int count;
   int layer;

   /* Determine how much we need to allocate. */
   if(clientCount == 0) {
      windows = NULL;
   } else {
      windows = AllocateStack(clientCount * sizeof(Window));
   }

   /* Set _NET_CLIENT_LIST */
   count = 0;
   for(np = taskBarNodes; np; np = np->next) {
      windows[count] = np->client->window;
      count += 1;
   }
   JXChangeProperty(display, rootWindow, atoms[ATOM_NET_CLIENT_LIST],
                    XA_WINDOW, 32, PropModeReplace,
                    (unsigned char*)windows, count);

   /* Set _NET_CLIENT_LIST_STACKING */
   count = 0;
   for(layer = FIRST_LAYER; layer <= LAST_LAYER; layer++) {
      for(client = nodes[layer]; client; client = client->next) {
         windows[count] = client->window;
         count += 1;
      }
   }
   JXChangeProperty(display, rootWindow, atoms[ATOM_NET_CLIENT_LIST_STACKING],
                    XA_WINDOW, 32, PropModeReplace,
                    (unsigned char*)windows, count);

   if(windows != NULL) {
      ReleaseStack(windows);
   }
   
}


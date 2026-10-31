// Production-linked converter/gate/visibility tests. No display or SDK needed.
#include <TrussC.h>
#include "../common/tcInjectedWindowTest.h"
#include <cstdio>
#include <limits>
using namespace tc;
namespace {
int checks = 0, failures = 0;
const internal::WindowContext* earlierContext = nullptr;
bool isEarlier(const internal::WindowContext& context) { return &context == earlierContext; }
void check(const char* name, bool value) { ++checks; if (!value) { ++failures; std::printf("host-entry: FAIL: %s\n", name); } }
using Native = tcCoreTest::InjectedWindow;
Window* mcpTarget = nullptr;
// Explicit subscriber ownership replaces the former ambient first-Window
// assumption. Test observers survive App swaps and retire at fixture scope end.
struct FixtureWindowScope {
    std::shared_ptr<App> observer;
    internal::AppOwnerScope owner;
    internal::WindowContextScope selected;
    Window* previous;
    explicit FixtureWindowScope(Window& w)
        : observer(internal::makeConstructedAppAt<App>(&w.context())),
          owner(internal::appOwner(*observer)), selected(&w.context()),
          previous(std::exchange(mcpTarget, &w)) {}
    ~FixtureWindowScope() { mcpTarget = previous; }
};
sapp_event event(sapp_event_type type, float x=0, float y=0, unsigned mods=0) {
    sapp_event e{}; e.type=type; e.mouse_x=x; e.mouse_y=y; e.modifiers=mods;
    e.key_code=SAPP_KEYCODE_A; e.mouse_button=SAPP_MOUSEBUTTON_LEFT; e.char_code=0x3042;
    return e;
}
void clearInput(Window& w) {
    auto& c=w.context(); c.pointerValid=false; c.pressedMouseButtons=c.snapshotMouseButtons=0;
    c.mousePressed=false; c.mouseButton=-1; c.mouseX=c.mouseY=c.lastPointerX=c.lastPointerY=0;
    c.touchMapper.reset(); c.keysPressed.clear();
    for(auto& owner:c.mousePressOwners) owner=nullptr;
}
#include "native-event-fixture.h"

template<class Record, class Cover, class Minimize>
void nativeVisibilityHistory(Window& w,Native& n,Record& record,Cover cover,Minimize minimize,bool startHidden,bool trust) {
    FixtureWindowScope fixture(w);
    auto& b=internal::WindowRequestAccess::binding(w);
    b.lastDeliveredHidden=false;b.visibilityReady=true;
    n.metadata.iconified=n.metadata.occluded=false;n.metadata.trust_hidden_signals=trust;
    std::vector<int> trace;
    auto raw=w.events().rawEvent.listen([&](const sapp_event& e){
        trace.push_back(e.type);
        check("native detail and aggregate see current visibility snapshot",w.context().suspended==w.isOccluded());
    });
    auto typed=w.events().occludedChanged.listen([&](OccludedEventArgs& e){trace.push_back(e.occluded?100:101);check("native typed value matches trusted snapshot",e.occluded==w.isOccluded());});
    auto expect=[&](std::initializer_list<int> expected){
        check("native full detail raw typed sequence",trace==(trust?std::vector<int>(expected):std::vector<int>{}));trace.clear();
    };
    cover(startHidden);expect(startHidden?std::initializer_list<int>{SAPP_EVENTTYPE_SUSPENDED,100}:std::initializer_list<int>{});
    cover(false);expect(startHidden?std::initializer_list<int>{SAPP_EVENTTYPE_RESUMED,101}:std::initializer_list<int>{});
    cover(true);expect({SAPP_EVENTTYPE_SUSPENDED,100});cover(true);expect({});
    cover(false);expect({SAPP_EVENTTYPE_RESUMED,101});cover(false);expect({});
    minimize(true);expect({SAPP_EVENTTYPE_ICONIFIED,SAPP_EVENTTYPE_SUSPENDED,100});
    cover(true);expect({});minimize(false);expect({SAPP_EVENTTYPE_RESTORED});
    cover(false);expect({SAPP_EVENTTYPE_RESUMED,101});
    cover(true);expect({SAPP_EVENTTYPE_SUSPENDED,100});minimize(true);expect({SAPP_EVENTTYPE_ICONIFIED});
    cover(false);expect({});minimize(false);expect({SAPP_EVENTTYPE_RESTORED,SAPP_EVENTTYPE_RESUMED,101});
    minimize(true);expect({SAPP_EVENTTYPE_ICONIFIED,SAPP_EVENTTYPE_SUSPENDED,100});
    minimize(false);expect({SAPP_EVENTTYPE_RESTORED,SAPP_EVENTTYPE_RESUMED,101});
    check("native producer retains role and draw policy independently",record.is_main==isEarlier(w.context()));
    n.metadata.trust_hidden_signals=true;
}
void nativeVisibilitySources(Window& w,Native& n) {
    FixtureWindowScope fixture(w);
    using namespace nativeFixture;
    for(bool trust:{false,true})for(bool allow:{false,true})for(bool startHidden:{false,true}) {
        mac::Record m(n);mac::NSWindow window;m.window=&window;mac::live=&m;
        m.is_main=isEarlier(w.context());m.trust_hidden_signals=trust;m.draw_while_hidden=allow;
        // Sample launch visibility before callback admission, then enter ready.
        auto& binding=internal::WindowRequestAccess::binding(w);
        binding.visibilityReady=false;binding.lastDeliveredHidden=false;
        n.events_enabled=[](){return false;};mac::observe(m,false,startHidden);
        n.metadata.iconified=m.iconified;n.metadata.occluded=m.occluded;n.metadata.trust_hidden_signals=trust;
        n.events_enabled=nullptr;
        std::vector<int> launch;
        auto launchRaw=w.events().rawEvent.listen([&](const sapp_event& e){launch.push_back(e.type);});
        auto launchTyped=w.events().occludedChanged.listen([&](OccludedEventArgs& e){launch.push_back(e.occluded?100:101);});
        {internal::WindowEntryScope ready(w);}
        check("launch-hidden native observation delivers aggregate at ready",launch==(trust&&startHidden?std::vector<int>{SAPP_EVENTTYPE_SUSPENDED,100}:std::vector<int>{}));
        launch.clear();mac::observe(m,false,false);
        check("launch-hidden macOS main and peer both resume",launch==(trust&&startHidden?std::vector<int>{SAPP_EVENTTYPE_RESUMED,101}:std::vector<int>{}));
        launchRaw.disconnect();launchTyped.disconnect();
        nativeVisibilityHistory(w,n,m,[&](bool covered){mac::observe(m,window.miniaturized,covered);},[&](bool iconic){window.miniaturized=iconic;if(iconic)mac::minimize(&m,nullptr);else mac::restore(&m,nullptr);},startHidden,trust);
        win::Record v(n);win::live=&v;v.is_main=isEarlier(w.context());v.trust_hidden_signals=trust;v.draw_while_hidden=allow;
        nativeVisibilityHistory(w,n,v,[&](bool covered){win::_sapp_tc_win32_observe_occlusion(&v,covered);},[&](bool iconic){win::minimize(&v,iconic);},startHidden,trust);
        x11::Record x(n);x11::live=&x;x.is_main=isEarlier(w.context());x.trust_hidden_signals=trust;x.draw_while_hidden=allow;
        nativeVisibilityHistory(w,n,x,[&](bool covered){x11::cover(&x,covered);},[&](bool iconic){x11::minimize(&x,iconic);},startHidden,trust);
    }
    n.metadata.iconified=n.metadata.occluded=false;n.metadata.trust_hidden_signals=true;
}
struct LockedNode:RectNode {
    int moves=0,drags=0,releases=0;Vec2 pos,delta;
    LockedNode(){setSize(80,80);enableEvents();}
    bool onMouseMove(const MouseMoveEventArgs& e)override{++moves;pos=e.pos;delta=e.delta;return false;}
    bool onMousePress(const MouseEventArgs&)override{return true;}
    bool onMouseDrag(const MouseDragEventArgs& e)override{++drags;pos=e.pos;delta=e.delta;return true;}
    bool onMouseRelease(const MouseEventArgs&)override{++releases;return true;}
};
struct LockedApp:App {
    int moves=0,drags=0,releases=0;Vec2 pos,delta;
    void mouseMoved(const MouseMoveEventArgs& e)override{++moves;pos=e.pos;delta=e.delta;}
    void mouseDragged(const MouseDragEventArgs& e)override{++drags;pos=e.pos;delta=e.delta;}
    void mouseReleased(const MouseEventArgs&)override{++releases;}
};
void lockedDelivery(Window& w,Native& original) {
    FixtureWindowScope fixture(w);
    for(bool pixels:{false,true})for(bool useXi2:{false,true}) {
        nativeFixture::xi2::Record n(w,original.win_id);
        n.metadata.dpi_scale=2;clearInput(w);internal::pixelPerfectMode()=pixels;
        auto app=internal::makeConstructedApp<LockedApp>();auto node=std::make_shared<LockedNode>();app->addChild(node);internal::WindowRequestAccess::attachCandidate(w, app);
        {internal::WindowEntryScope entry(w);internal::setupNodeOnce(*app);}
        n.send(event(SAPP_EVENTTYPE_MOUSE_MOVE,20,30));
        auto motion=[&](float dx,float dy){
            if(useXi2)nativeFixture::xi2::motion(n,dx,dy);
            else{auto e=event(SAPP_EVENTTYPE_MOUSE_MOVE,999,888);e.pointer_locked=true;e.mouse_dx=dx;e.mouse_dy=dy;n.send(e);}
        };
        motion(8,-4);
        const Vec2 expectedPos(pixels?20:10,pixels?30:15),expectedDelta(pixels?8:4,pixels?-4:-2);
        check("locked motion reaches App virtual mouseMoved",app->moves==2&&app->pos==expectedPos&&app->delta==expectedDelta);
        check("locked motion reaches Node mouse move",node->moves==2&&node->pos==expectedPos&&node->delta==expectedDelta);
        n.send(event(SAPP_EVENTTYPE_MOUSE_DOWN,20,30));
        check("press grabs the actual Node",w.context().grabbedNode.lock()==node&&w.context().grabbedButton==0);
        motion(8,-4);
        check("locked motion reaches App virtual mouseDragged",app->drags==1&&app->pos==expectedPos&&app->delta==expectedDelta);
        check("locked drag reaches grabbed Node and retains grab",node->drags==1&&node->pos==expectedPos&&node->delta==expectedDelta&&w.context().grabbedNode.lock()==node&&app->releases==0&&node->releases==0);
        auto unlock=w.events().rawEvent.listen([&](const sapp_event& e){if(e.type==SAPP_EVENTTYPE_MOUSE_MOVE){n.pointerLocked=false;nativeFixture::xi2::_sapp_tc.mouse_locked=false;}});
        nativeFixture::xi2::_sapp_tc.mouse_locked=true;motion(8,-4);unlock.disconnect();
        check("raw unlock keeps locked App and Node drag and grab for current packet",app->drags==2&&node->drags==2&&app->delta==expectedDelta&&node->delta==expectedDelta&&w.context().grabbedNode.lock()==node&&node->releases==0);
        n.send(event(SAPP_EVENTTYPE_MOUSE_MOVE,40,50));
        check("next unlocked missing-button motion releases App and Node once",app->releases==1&&node->releases==1&&w.context().grabbedNode.expired()&&w.context().grabbedButton==-1&&w.context().pressedMouseButtons==0);
        n.send(event(SAPP_EVENTTYPE_MOUSE_UP,40,50));
        check("late physical UP does not repeat recovered App or Node release",app->releases==1&&node->releases==1);
        n.send(event(SAPP_EVENTTYPE_MOUSE_DOWN,20,30));nativeFixture::xi2::_sapp_tc.mouse_locked=true;motion(2,3);
        n.send(event(SAPP_EVENTTYPE_MOUSE_UP,20,30));
        check("physical UP after locked drag clears grab once",app->releases==2&&node->releases==2&&w.context().grabbedNode.expired()&&w.context().pressedMouseButtons==0);
        if(useXi2)check("real XI2 cookie parsing queries and frees native data",nativeFixture::xi2::queries>0&&nativeFixture::xi2::freed==nativeFixture::xi2::queries);
        w.destroyApp();internal::WindowRequestAccess::applyPendingApp(w);
    }
    internal::bindWindow(w,{original.win_id},original.operations,&original);internal::pixelPerfectMode()=false;
}
void pointers(Window& w, Native& n) {
    FixtureWindowScope fixture(w);
    for (bool pixels : {false,true}) {
        internal::pixelPerfectMode()=pixels; clearInput(w); n.metadata.dpi_scale=1;
        Vec2 pos, delta, scroll, release, pressPosition; int releases=0, presses=0, drags=0;
        auto moved=w.events().mouseMoved.listen([&](MouseMoveEventArgs& e){pos=e.pos;delta=e.delta;check("move global fields equal window fields",e.pos==e.globalPos && e.delta==e.globalDelta);});
        auto dragged=w.events().mouseDragged.listen([&](MouseDragEventArgs& e){++drags;pos=e.pos;delta=e.delta;});
        auto pressed=w.events().mousePressed.listen([&](MouseEventArgs& e){++presses;pressPosition=e.pos;});
        auto released=w.events().mouseReleased.listen([&](MouseEventArgs& e){++releases;release=e.pos;});
        auto scrolled=w.events().mouseScrolled.listen([&](ScrollEventArgs& e){scroll=e.pos;});
        n.send(event(SAPP_EVENTTYPE_MOUSE_MOVE,100,40));
        check("first move has zero delta",pos==Vec2(100,40)&&delta==Vec2(0,0));
        auto consumed=w.events().mouseMoved.listen([](MouseMoveEventArgs& e){e.consumed=true;},-100);
        n.send(event(SAPP_EVENTTYPE_MOUSE_MOVE,110,50)); consumed.disconnect();
        n.send(event(SAPP_EVENTTYPE_MOUSE_MOVE,120,60));
        check("consumed motion still advances anchor",delta==Vec2(10,10));
        n.send(event(SAPP_EVENTTYPE_MOUSE_DOWN,100,40));
        n.metadata.dpi_scale=2;
        auto move=event(SAPP_EVENTTYPE_MOUSE_MOVE,220,100,SAPP_MODIFIER_LMB); move.mouse_dx=999;move.mouse_dy=-999;
        n.send(move);
        check("DPI change converts previous position before subtracting",pos==Vec2(pixels?220:110,pixels?100:50)&&delta==Vec2(pixels?20:10,pixels?20:10));
        auto locked=event(SAPP_EVENTTYPE_MOUSE_MOVE,999,888);locked.pointer_locked=true;locked.mouse_dx=8;locked.mouse_dy=-4;
        n.send(locked);
        check("locked pointer fixes position and uses scaled relative delta",pos==Vec2(pixels?220:110,pixels?100:50)&&delta==Vec2(pixels?8:4,pixels?-4:-2)&&releases==0);
        n.pointerLocked=true;
        auto unlock=w.events().rawEvent.listen([&](const sapp_event& e){if(e.type==SAPP_EVENTTYPE_MOUSE_MOVE)n.pointerLocked=false;});
        auto lockedAgain=event(SAPP_EVENTTYPE_MOUSE_MOVE,999,888);lockedAgain.mouse_dx=6;lockedAgain.mouse_dy=-2;n.send(lockedAgain);unlock.disconnect();
        check("raw listener unlock preserves current locked delta and press",!n.pointerLocked&&pos==Vec2(pixels?220:110,pixels?100:50)&&delta==Vec2(pixels?6:3,pixels?-2:-1)&&releases==0);
        n.send(event(SAPP_EVENTTYPE_MOUSE_ENTER,260,120,SAPP_MODIFIER_LMB));
        check("ENTER includes jump and delivers drag",pos==Vec2(pixels?260:130,pixels?120:60)&&delta==Vec2(pixels?40:20,pixels?20:10)&&drags==4);
        n.send(event(SAPP_EVENTTYPE_MOUSE_SCROLL));
        check("scroll after enter uses entered position",scroll==pos);
        n.send(event(SAPP_EVENTTYPE_MOUSE_LEAVE));
        check("leave parks hover only",w.context().mouseX==-1&&w.context().lastPointerX==pos.x);
        n.send(event(SAPP_EVENTTYPE_UNFOCUSED));
        check("leave then lost release uses last real position",releases==1&&release==pos);
        n.send(event(SAPP_EVENTTYPE_MOUSE_UP,280,140));
        check("late UP does not duplicate recovered release",releases==1);
        n.send(event(SAPP_EVENTTYPE_MOUSE_ENTER,300,160));
        check("UP real position becomes next anchor",delta==Vec2(pixels?20:10,pixels?20:10));
        clearInput(w); n.metadata.dpi_scale=1.5f;
        n.send(event(SAPP_EVENTTYPE_MOUSE_MOVE,100.1f,40.1f));n.send(event(SAPP_EVENTTYPE_MOUSE_MOVE,100.1f,40.1f));
        check("same position at fractional DPI has exactly zero delta",delta==Vec2(0,0));n.metadata.dpi_scale=2;
        clearInput(w); auto firstLocked=event(SAPP_EVENTTYPE_MOUSE_MOVE,20,30);firstLocked.pointer_locked=true;firstLocked.mouse_dx=50;n.send(firstLocked);
        check("first locked event also has zero delta",delta==Vec2(0,0));
        clearInput(w); n.send(event(SAPP_EVENTTYPE_MOUSE_LEAVE)); n.send(event(SAPP_EVENTTYPE_MOUSE_ENTER,20,30));
        check("initial leave cannot create position anchor",delta==Vec2(0,0));
        clearInput(w); n.metadata.dpi_scale=1;
        auto snapshot=event(SAPP_EVENTTYPE_MOUSE_MOVE,10,20,SAPP_MODIFIER_RMB);snapshot.button_snapshot=true;
        n.send(snapshot);
        check("drag-only snapshot does not fabricate press",presses==1&&w.context().snapshotMouseButtons==2&&w.context().mouseButton==1&&delta==Vec2(0,0));
        n.send(event(SAPP_EVENTTYPE_MOUSE_LEAVE)); n.send(event(SAPP_EVENTTYPE_MOUSE_MOVE,30,40));
        check("missing snapshot-held button recovers release once",releases==2&&release==Vec2(10,20)&&w.context().snapshotMouseButtons==0);
        auto up=event(SAPP_EVENTTYPE_MOUSE_UP,30,40);up.mouse_button=SAPP_MOUSEBUTTON_RIGHT;n.send(up);
        n.send(event(SAPP_EVENTTYPE_UNFOCUSED));check("snapshot late UP and unfocus cannot repeat release",releases==2);
        // The same position rule applies to the primary finger.
        clearInput(w);n.metadata.dpi_scale=1;
        auto touch=event(SAPP_EVENTTYPE_TOUCHES_BEGAN);touch.num_touches=1;
        touch.touches[0]={42,100,40,SAPP_ANDROIDTOOLTYPE_FINGER,true};n.send(touch);
        n.metadata.dpi_scale=2;touch.type=SAPP_EVENTTYPE_TOUCHES_MOVED;touch.touches[0].pos_x=220;touch.touches[0].pos_y=100;n.send(touch);
        check("touch drag uses same DPI difference",delta==Vec2(pixels?20:10,pixels?20:10));
        n.metadata.dpi_scale=3;touch.type=SAPP_EVENTTYPE_TOUCHES_CANCELLED;touch.touches[0].pos_x=330;touch.touches[0].pos_y=150;n.send(touch);
        check("touch release anchor is re-expressed after DPI change",release==Vec2(pixels?330:110,pixels?150:50));
        check("touch cancel clears held ledger",w.context().pressedMouseButtons==0);
        clearInput(w);n.metadata.dpi_scale=1;n.send(event(SAPP_EVENTTYPE_MOUSE_MOVE,100,40));n.pointerLocked=true;
        touch.type=SAPP_EVENTTYPE_TOUCHES_BEGAN;touch.touches[0].pos_x=20;touch.touches[0].pos_y=30;n.send(touch);
        check("locked touch press uses fixed mouse anchor",pressPosition==Vec2(100,40));
        touch.type=SAPP_EVENTTYPE_TOUCHES_MOVED;touch.touches[0].pos_x=50;touch.mouse_dx=4;touch.mouse_dy=-2;n.send(touch);
        check("locked touch drag shares fixed position and relative delta",pos==Vec2(100,40)&&delta==Vec2(4,-2));
        touch.type=SAPP_EVENTTYPE_TOUCHES_ENDED;n.send(touch);n.pointerLocked=false;
        check("locked touch release keeps fixed mouse anchor",release==Vec2(100,40)&&w.context().snapshotMouseButtons==0);
    }
    internal::pixelPerfectMode()=false;
}
// Contrary observations may arrive while either half of a transition runs.
void reentrantVisibility(Window& w, Native& n) {
    FixtureWindowScope fixture(w);
    for (bool hidden : {false,true}) for (bool fromTyped : {false,true}) {
        auto& binding=internal::WindowRequestAccess::binding(w);
        n.metadata.iconified=false;n.metadata.occluded=!hidden;n.metadata.trust_hidden_signals=true;
        {internal::WindowEntryScope refresh(w);}
        binding.lastDeliveredHidden=!hidden;binding.visibilityReady=true;
        std::vector<bool> first,tail,notices;bool nested=false;
        auto log=getLogger().onLog.listen([&](LogEventArgs& e){
            if(e.message.find("Window is hidden")!=std::string::npos)notices.push_back(true);
            else if(e.message.find("Window is visible")!=std::string::npos)notices.push_back(false);
        });
        auto contrary=[&]{if(nested)return;nested=true;n.metadata.occluded=!hidden;n.send(event(hidden?SAPP_EVENTTYPE_RESUMED:SAPP_EVENTTYPE_SUSPENDED));};
        auto raw=w.events().rawEvent.listen([&](const sapp_event& e){if(!fromTyped&&e.type==(hidden?SAPP_EVENTTYPE_SUSPENDED:SAPP_EVENTTYPE_RESUMED))contrary();});
        auto typed=w.events().occludedChanged.listen([&](OccludedEventArgs& e){
            first.push_back(e.occluded);check("reentrant typed entry agrees with getter and context",e.occluded==w.isOccluded()&&e.occluded==w.context().suspended);
            if(fromTyped)contrary();
        });
        auto last=w.events().occludedChanged.listen([&](OccludedEventArgs& e){tail.push_back(e.occluded);check("typed tail never receives superseded visibility",e.occluded==w.isOccluded()&&e.occluded==w.context().suspended);});
        n.metadata.occluded=hidden;n.send(event(hidden?SAPP_EVENTTYPE_SUSPENDED:SAPP_EVENTTYPE_RESUMED));
        check("contrary raw or typed observation has final current value",nested&&first==(fromTyped?std::vector<bool>{hidden,!hidden}:std::vector<bool>{!hidden})&&tail==std::vector<bool>{!hidden}&&tail.back()==w.isOccluded()&&binding.lastDeliveredHidden==!hidden);
        n.send(event(hidden?SAPP_EVENTTYPE_RESUMED:SAPP_EVENTTYPE_SUSPENDED));
        check("superseded visibility is never retried",tail.size()==1);
        check("visibility Notices omit superseded outer transition",notices==(fromTyped?std::vector<bool>{hidden,!hidden}:std::vector<bool>{!hidden}));
    }
}
void visibility(Window& w, Native& n, Window& peer) {
    FixtureWindowScope fixture(w);
    auto& b=internal::WindowRequestAccess::binding(w);
    n.metadata.iconified=n.metadata.occluded=false;b.lastDeliveredHidden=false;
    std::vector<int> order;int typed=0,peerCount=0;
    auto p=peer.events().occludedChanged.listen([&](OccludedEventArgs&){++peerCount;});
    auto raw=w.events().rawEvent.listen([&](const sapp_event& e){
        check("ctx and getter agree before every raw listener",w.context().suspended==w.isOccluded());
        if(e.type==SAPP_EVENTTYPE_SUSPENDED)order.push_back(1);
        if(e.type==SAPP_EVENTTYPE_RESUMED)order.push_back(3);
        if(e.type==SAPP_EVENTTYPE_ICONIFIED||e.type==SAPP_EVENTTYPE_RESTORED)order.push_back(0);
    });
    auto listener=w.events().occludedChanged.listen([&](OccludedEventArgs& e){++typed;order.push_back(e.occluded?2:4);check("typed receiver and value",&events()==&w.events()&&e.occluded==w.isOccluded());});
    b.visibilityReady=true;n.metadata.iconified=true;
    { internal::WindowEntryScope poll(w); }
    check("later metadata polling waits for native aggregate event",typed==0&&!b.lastDeliveredHidden);
    n.send(event(SAPP_EVENTTYPE_RESUMED));
    check("mismatched aggregate does not consume pending hidden state",order.empty()&&typed==0&&!b.lastDeliveredHidden);
    n.send(event(SAPP_EVENTTYPE_ICONIFIED));
    check("detail refresh never consumes aggregate delivery",typed==0&&!b.lastDeliveredHidden);
    n.send(event(SAPP_EVENTTYPE_SUSPENDED));
    check("minimize detail then raw then typed",order==std::vector<int>{0,1,2});
    n.metadata.occluded=true;n.send(event(SAPP_EVENTTYPE_SUSPENDED));
    n.metadata.iconified=false;n.send(event(SAPP_EVENTTYPE_RESTORED));n.send(event(SAPP_EVENTTYPE_SUSPENDED));
    check("overlapping minimize and occlusion stays hidden once",typed==1&&w.isOccluded());
    n.metadata.occluded=false;n.send(event(SAPP_EVENTTYPE_RESUMED));n.send(event(SAPP_EVENTTYPE_RESUMED));
    check("duplicates silent and final reveal once",typed==2&&order==std::vector<int>{0,1,2,0,3,4}&&peerCount==0);
    {auto late=w.events().occludedChanged.listen([&](OccludedEventArgs&){++typed;});check("late subscription never replays",typed==2);}
    w.setFps(30);w.setIndependentFps(20,10);n.send(event(SAPP_EVENTTYPE_FOCUSED));
    check("policy setters do not emit visibility",typed==2);
    n.metadata.iconified=n.metadata.occluded=true;n.metadata.trust_hidden_signals=false;
    n.send(event(SAPP_EVENTTYPE_ICONIFIED));n.send(event(SAPP_EVENTTYPE_SUSPENDED));n.send(event(SAPP_EVENTTYPE_RESUMED));
    check("trust off clears effective state without events",!w.isOccluded()&&!w.context().suspended&&typed==2&&!b.lastDeliveredHidden);
    n.metadata.trust_hidden_signals=true;n.metadata.iconified=n.metadata.occluded=false;
    EventListener removed;int later=0;
    auto remove=w.events().occludedChanged.listen([&](OccludedEventArgs&){removed.disconnect();},-100);
    removed=w.events().occludedChanged.listen([&](OccludedEventArgs&){++later;});
    n.metadata.occluded=true;n.send(event(SAPP_EVENTTYPE_SUSPENDED));check("removal during typed delivery honored",later==0&&typed==3);
    remove.disconnect();listener.disconnect();
    auto throws=w.events().occludedChanged.listen([](OccludedEventArgs&){throw 239;},-100);
    auto after=w.events().occludedChanged.listen([&](OccludedEventArgs&){++later;});
    n.metadata.occluded=false;n.send(event(SAPP_EVENTTYPE_RESUMED));n.send(event(SAPP_EVENTTYPE_RESUMED));
    check("typed throw aborts remaining listeners and is not retried",later==0&&!b.lastDeliveredHidden);
    throws.disconnect();after.disconnect();
    auto rawThrow=w.events().rawEvent.listen([](const sapp_event& e){if(e.type==SAPP_EVENTTYPE_SUSPENDED)throw 997;},-100);
    int attempts=0;auto t=w.events().occludedChanged.listen([&](OccludedEventArgs& e){++attempts;order.push_back(e.occluded?2:4);});
    n.metadata.occluded=true;n.send(event(SAPP_EVENTTYPE_SUSPENDED));rawThrow.disconnect();n.send(event(SAPP_EVENTTYPE_SUSPENDED));
    check("raw throw commits transition and never retries typed",attempts==0&&b.lastDeliveredHidden&&w.isOccluded());
    n.metadata.occluded=false;n.send(event(SAPP_EVENTTYPE_RESUMED));check("later transition survives throw",attempts==1);
    // Hidden launch is state, with one attempt at the first ready entry.
    for(bool minimized:{false,true}) {
        n.metadata.iconified=minimized;n.metadata.occluded=!minimized;b.lastDeliveredHidden=false;b.visibilityReady=false;attempts=0;order.clear();
        { internal::WindowEntryScope ready(w);check("ready entry admitted",bool(ready)); }
        check("already hidden ready entry delivers one raw then typed",attempts==1&&order==std::vector<int>{1,2});
        { internal::WindowEntryScope ready(w); }
        auto late=w.events().occludedChanged.listen([&](OccludedEventArgs&){++attempts;});
        check("hidden state not replayed on another entry or subscription",attempts==1);
        n.metadata.iconified=n.metadata.occluded=false;n.send(event(SAPP_EVENTTYPE_RESUMED));
    }
    n.metadata.iconified=false;n.metadata.occluded=true;n.metadata.trust_hidden_signals=true;
    n.send(event(SAPP_EVENTTYPE_SUSPENDED));const int beforeOptOut=attempts;const auto rawBeforeOptOut=order.size();
    n.metadata.trust_hidden_signals=false;n.send(event(SAPP_EVENTTYPE_RESUMED));
    check("trust opt-out of an observed hidden state emits no visible transition",attempts==beforeOptOut&&order.size()==rawBeforeOptOut&&!b.lastDeliveredHidden&&!w.isOccluded()&&!w.context().suspended);
    n.metadata.occluded=false;n.metadata.trust_hidden_signals=true;

}
void parity(Window& w, Native& n, std::vector<int>& counts) {
    FixtureWindowScope fixture(w);
    std::vector<int> types;
    for(int type=0;type<_SAPP_EVENTTYPE_NUM;++type) types.push_back(type);
    types.push_back(_SAPP_EVENTTYPE_NUM);types.push_back(_SAPP_EVENTTYPE_FORCE_U32);
    for(int type:types) {
        clearInput(w);n.metadata.iconified=n.metadata.occluded=false;n.metadata.trust_hidden_signals=true;
        auto& b=internal::WindowRequestAccess::binding(w);b.lastDeliveredHidden=false;
        int rawCount=0,typed=0;bool rawFirst=true;
        std::vector<EventListener> listeners;
        listeners.push_back(w.events().rawEvent.listen([&](const sapp_event& e){++rawCount;check("native target source and dimensions",e.window_id==n.win_id&&e.source==SAPP_EVENTSOURCE_REAL&&e.frame_count==123&&e.dpi_scale==n.metadata.dpi_scale&&e.framebuffer_width==n.metadata.fb_width);check("input wake precedes callback",!_sapp_tc_event_wakes(e.type)||n.input_delivered);}));
        const bool processEvent = type == SAPP_EVENTTYPE_QUIT_REQUESTED || type == SAPP_EVENTTYPE_TC_DEVICE_LOST;
        auto notify=[&](auto&){rawFirst &= rawCount == (processEvent ? 0 : 1); ++typed;};
        listeners.push_back(w.events().keyPressed.listen(notify));listeners.push_back(w.events().keyReleased.listen(notify));
        listeners.push_back(w.events().mousePressed.listen(notify));listeners.push_back(w.events().mouseReleased.listen(notify));
        listeners.push_back(w.events().mouseMoved.listen(notify));listeners.push_back(w.events().mouseDragged.listen(notify));
        listeners.push_back(w.events().mouseScrolled.listen(notify));listeners.push_back(w.events().touchPressed.listen(notify));
        listeners.push_back(w.events().touchMoved.listen(notify));listeners.push_back(w.events().touchReleased.listen(notify));
        listeners.push_back(w.events().clipboardPasted.listen(notify));listeners.push_back(w.events().filesDropped.listen(notify));
        listeners.push_back(w.events().occludedChanged.listen(notify));
        listeners.push_back(runtime().events().deviceLost.listen([&](DeviceLostEventArgs& e){notify(e);e.cancel=true;}));
        listeners.push_back(runtime().events().exitRequested.listen([&](ExitRequestEventArgs& e){notify(e);e.cancel=true;}));
        auto e=event(static_cast<sapp_event_type>(type));n.input_delivered=false;
        if(type==SAPP_EVENTTYPE_MOUSE_UP)w.context().pressedMouseButtons=1;
        if(type==SAPP_EVENTTYPE_SUSPENDED)n.metadata.occluded=true;
        if(type==SAPP_EVENTTYPE_RESUMED)b.lastDeliveredHidden=true;
        n.send(e);
        // Expected typed notifications by sapp_event_type ordinal. Touch has no points here.
        constexpr int expected[]={0,1,1,0,1,1,1,1,1,0,1,1,1,1,0,0,0,0,0,1,1,1,1,1,1};
        static_assert(std::size(expected)==_SAPP_EVENTTYPE_NUM);
        constexpr bool wakes[]={false,true,true,true,true,true,true,true,true,true,true,true,true,true,false,true,true,true,true,false,false,false,true,true,false};
        static_assert(std::size(wakes)==_SAPP_EVENTTYPE_NUM);
        check("exhaustive typed parity table",typed==(type<_SAPP_EVENTTYPE_NUM?expected[type]:0)&&rawCount==(processEvent ? 0 : 1)&&rawFirst);
        check("exhaustive wake classifier",_sapp_tc_event_wakes(e.type)==(type<_SAPP_EVENTTYPE_NUM?wakes[type]:false));
        constexpr const char* names[]={"a rawEvent listener","keyPressed()","keyReleased()","a rawEvent listener","mousePressed()","mouseReleased()","mouseScrolled()","mouseMoved()","mouseMoved()","a rawEvent listener","touchPressed()","touchMoved()","touchReleased()","touchReleased()","windowResized()","a rawEvent listener","a rawEvent listener","a rawEvent listener","a rawEvent listener","a rawEvent listener","a rawEvent listener","the exitRequested event","the clipboardPasted event","filesDropped()","the deviceLost event"};
        static_assert(std::size(names)==_SAPP_EVENTTYPE_NUM);
        check("exhaustive diagnostic classifier",std::string(internal::eventEntryName(&e,false))==(type<_SAPP_EVENTTYPE_NUM?names[type]:"a rawEvent listener"));
        counts.push_back(typed);
    }
}
json tool(const char* name,json args) {
    args["window"] = internal::WindowRequestAccess::creationId(*mcpTarget);
    auto reply=json::parse(mcp::internal::Server::instance().processMessage(json{{"jsonrpc","2.0"},{"id",239},{"method","tools/call"},{"params",{{"name",name},{"arguments",args}}}}.dump()));
    check("MCP tool succeeds",reply.contains("result")&&!reply["result"].value("isError",false));return reply;
}
void injections(Window& w,Native& n,Window& peer) {
    FixtureWindowScope fixture(w);
    clearInput(w);internal::pixelPerfectMode()=false;n.metadata.dpi_scale=2;
    internal::currentWindowCtx()=&peer.context();mcp::setToolLevel(mcp::ToolLevel::Control);
    Vec2 pos,delta;int raw=0,press=0,release=0,chars=0;bool injected=true;std::shared_ptr<App> oldApp;
    auto r=w.events().rawEvent.listen([&](const sapp_event& e){++raw;if(e.type==SAPP_EVENTTYPE_MOUSE_DOWN)check("MCP raw press has held button bit",e.modifiers&SAPP_MODIFIER_LMB);if(e.type==SAPP_EVENTTYPE_MOUSE_UP)check("MCP raw release clears button bit",!(e.modifiers&SAPP_MODIFIER_LMB));injected&=e.source==SAPP_EVENTSOURCE_INJECTED&&e.window_id==n.win_id;if(e.type==SAPP_EVENTTYPE_CHAR)++chars;});
    auto m=w.events().mouseMoved.listen([&](MouseMoveEventArgs& e){pos=e.pos;delta=e.delta;});
    auto d=w.events().mouseDragged.listen([&](MouseDragEventArgs& e){pos=e.pos;delta=e.delta;});
    auto p=w.events().mousePressed.listen([&](MouseEventArgs& e){++press;check("temporary click modifiers held through callback",!e.shift||isShiftPressed());});
    auto u=w.events().mouseReleased.listen([&](MouseEventArgs&){++release;});
    tool("tc_mouse_move",{{"x",100},{"y",40}});check("MCP main target first delta zero",pos==Vec2(100,40)&&delta==Vec2(0,0)&&peer.context().mouseX==0);
    n.metadata.dpi_scale=1;tool("tc_mouse_move",{{"x",110},{"y",50},{"button",0}});
    check("MCP drag-only target anchor and DPI",pos==Vec2(110,50)&&delta==Vec2(10,10)&&press==0);
    tool("tc_mouse_move",{{"x",120},{"y",60}});check("MCP snapshot release recovery separate from real press",release==1);
    tool("tc_mouse_release",{{"x",120},{"y",60}});check("MCP recovered UP does not duplicate",release==1);
    tool("tc_mouse_press",{{"x",20},{"y",30}});tool("tc_mouse_move",{{"x",25},{"y",35},{"button",0}});tool("tc_mouse_release",{{"x",25},{"y",35}});
    check("real MCP press drag release",press==1&&release==2&&delta==Vec2(5,5));
    auto consumed=w.events().mousePressed.listen([](MouseEventArgs& e){e.consumed=true;},-100);
    tool("tc_mouse_click",{{"x",40},{"y",50},{"shift",true}});check("click release has fresh consumption and modifiers restore",release==3&&!w.context().keysPressed.contains(SAPP_KEYCODE_LEFT_SHIFT));consumed.disconnect();
    tool("tc_key_press",{{"key",SAPP_KEYCODE_A},{"shift",true}});tool("tc_key_release",{{"key",SAPP_KEYCODE_A},{"shift",true}});
    check("key input never guesses CHAR",chars==0&&injected&&raw==13&&w.context().keysPressed.empty());
    check("MCP restores peer caller",internal::currentWindowCtx()==&peer.context());
    auto replay=event(SAPP_EVENTTYPE_CHAR);replay.source=SAPP_EVENTSOURCE_REPLAYED;
    check("explicit replay text admitted",n.operations.injectEvent({n.win_id},&n,&replay)&&chars==1);
    const int before=raw;
    auto invalid=event(SAPP_EVENTTYPE_MOUSE_MOVE,std::numeric_limits<float>::infinity());
    check("native invalid coordinates reject before state change",!n.operations.injectEvent({n.win_id},&n,&invalid)&&raw==before);
    invalid=event(SAPP_EVENTTYPE_TOUCHES_MOVED);invalid.num_touches=SAPP_MAX_TOUCHPOINTS+1;
    check("native oversized touch array rejected",!n.operations.injectEvent({n.win_id},&n,&invalid)&&raw==before);
    invalid.num_touches=-1;check("native negative touch count rejected",!n.operations.injectEvent({n.win_id},&n,&invalid));
    invalid=event(SAPP_EVENTTYPE_SUSPENDED);check("injection cannot change OS visibility",!n.operations.injectEvent({n.win_id},&n,&invalid));
    invalid=event(SAPP_EVENTTYPE_MOUSE_MOVE);check("stale injection target rejected",!n.operations.injectEvent({n.win_id+1},&n,&invalid));
    const int rawBefore=raw;
    int invalidCallbacks=0;
    struct InvalidApp : App { int& calls; explicit InvalidApp(int& calls):calls(calls){} void windowResized(int,int)override{++calls;} };
    auto pendingInvalid=internal::makeConstructedApp<InvalidApp>(invalidCallbacks);w.pendingApp_=pendingInvalid;w.appRequested_=true;
    const auto beforeInvalidApp=w.getApp();
    n.metadata.dpi_scale=2;
    auto rejected=json::parse(mcp::internal::Server::instance().processMessage(json{{"jsonrpc","2.0"},{"id",239},{"method","tools/call"},{"params",{{"name","tc_mouse_move"},{"arguments",{{"window",n.win_id},{"x",std::numeric_limits<float>::max()},{"y",0}}}}}}.dump()));
    check("MCP transformed overflow rejected before callbacks",rejected.contains("error")&&rejected["error"]["code"]==-32602&&raw==rawBefore&&invalidCallbacks==0&&w.getApp()==beforeInvalidApp&&w.pendingApp_==pendingInvalid);
    w.pendingApp_.reset();w.appRequested_=false;
    Vec2 freshRelease;
    auto changeDpi=w.events().mousePressed.listen([&](MouseEventArgs&){n.metadata.dpi_scale=3;});
    auto releasePosition=w.events().mouseReleased.listen([&](MouseEventArgs& e){freshRelease=e.pos;});
    tool("tc_mouse_click",{{"x",10},{"y",10}});
    check("MCP release rebuilds coordinates after callback DPI change",freshRelease==Vec2(10,10));changeDpi.disconnect();releasePosition.disconnect();
    struct GestureApp : App { int presses=0,releases=0; void mousePressed(const MouseEventArgs&)override{++presses;}void mouseReleased(const MouseEventArgs&)override{++releases;} };
    auto first=internal::makeConstructedApp<GestureApp>(),next=internal::makeConstructedApp<GestureApp>();internal::WindowRequestAccess::attachCandidate(w, first);
    auto swap=w.events().mousePressed.listen([&](MouseEventArgs&){internal::WindowRequestAccess::attachCandidate(w, next);});
    tool("tc_mouse_click",{{"x",10},{"y",10}});
    check("MCP click holds attachment across both halves",first->presses==1&&first->releases==1&&next->presses==0&&next->releases==0&&w.getApp()==next);
    swap.disconnect();w.destroyApp();internal::WindowRequestAccess::applyPendingApp(w);
    auto pendingCloseApp=internal::makeConstructedApp<GestureApp>();internal::WindowRequestAccess::attachCandidate(w, pendingCloseApp);
    auto closeDuringPress=w.events().mousePressed.listen([&](MouseEventArgs&){n.closed=true;});
    tool("tc_mouse_click",{{"x",10},{"y",10}});
    check("pending native close cannot split an admitted click",pendingCloseApp->presses==1&&pendingCloseApp->releases==1);
    n.closed=false;closeDuringPress.disconnect();w.destroyApp();internal::WindowRequestAccess::applyPendingApp(w);
    auto nestedKey=w.events().mousePressed.listen([&](MouseEventArgs&){auto key=event(SAPP_EVENTTYPE_KEY_DOWN);key.key_code=SAPP_KEYCODE_B;key.modifiers=SAPP_MODIFIER_SHIFT;n.operations.injectEvent({n.win_id},&n,&key);});
    tool("tc_mouse_click",{{"x",10},{"y",10},{"shift",true}});
    check("temporary click modifiers preserve callback changes to other keys",w.context().keysPressed.contains(SAPP_KEYCODE_B)&&!w.context().keysPressed.contains(SAPP_KEYCODE_LEFT_SHIFT));
    nestedKey.disconnect();auto keyUp=event(SAPP_EVENTTYPE_KEY_UP);keyUp.key_code=SAPP_KEYCODE_B;n.operations.injectEvent({n.win_id},&n,&keyUp);
    auto fail=w.events().rawEvent.listen([](const sapp_event& e){if(e.type==SAPP_EVENTTYPE_MOUSE_DOWN)throw 239;});
    tool("tc_mouse_click",{{"x",10},{"y",10},{"shift",true}});
    check("click modifier restoration survives failed raw delivery",!w.context().keysPressed.contains(SAPP_KEYCODE_LEFT_SHIFT));
    internal::currentWindowCtx()=nullptr;
}
void clickRecovery(Window& w, Native& n) {
    FixtureWindowScope fixture(w);
    clearInput(w);internal::pixelPerfectMode()=false;n.metadata.dpi_scale=1;
    bool shift=false,ctrl=false,alt=false,super=false;int presses=0,releases=0,raw=0;
    auto p=w.events().mousePressed.listen([&](MouseEventArgs& e){
        ++presses;shift=e.shift;ctrl=e.ctrl;alt=e.alt;super=e.super;
        check("click replaces held modifier keys during press",isShiftPressed()==e.shift&&isControlPressed()==e.ctrl&&isAltPressed()==e.alt&&isSuperPressed()==e.super);
    });
    auto u=w.events().mouseReleased.listen([&](MouseEventArgs& e){++releases;check("click keeps exact requested flags on release",e.shift==shift&&e.ctrl==ctrl&&e.alt==alt&&e.super==super);});
    auto r=w.events().rawEvent.listen([&](const sapp_event& e){
        if(e.type==SAPP_EVENTTYPE_MOUSE_DOWN||e.type==SAPP_EVENTTYPE_MOUSE_UP){++raw;check("raw click flags clear held keys",!(e.modifiers&(SAPP_MODIFIER_CTRL|SAPP_MODIFIER_ALT|SAPP_MODIFIER_SUPER)));}
    });
    for(int key:{SAPP_KEYCODE_RIGHT_SHIFT,SAPP_KEYCODE_RIGHT_CONTROL,SAPP_KEYCODE_RIGHT_ALT,SAPP_KEYCODE_RIGHT_SUPER})tool("tc_key_press",{{"key",key}});
    for(bool explicitFalse:{false,true}) {
        json args={{"x",10},{"y",20}};if(explicitFalse){args["shift"]=false;args["ctrl"]=false;args["alt"]=false;args["super"]=false;}
        tool("tc_mouse_click",args);
        check("default and explicit false clicks clear all modifiers",!shift&&!ctrl&&!alt&&!super&&w.context().keysPressed.size()==4);
    }
    tool("tc_mouse_click",{{"x",10},{"y",20},{"shift",true}});
    check("click can select exactly one modifier over held others",shift&&!ctrl&&!alt&&!super&&w.context().keysPressed.size()==4);
    r.disconnect();
    for(int key:{SAPP_KEYCODE_RIGHT_SHIFT,SAPP_KEYCODE_RIGHT_CONTROL,SAPP_KEYCODE_RIGHT_ALT,SAPP_KEYCODE_RIGHT_SUPER})tool("tc_key_release",{{"key",key}});
    clearInput(w);const int before=releases;
    auto dpi=w.events().mousePressed.listen([&](MouseEventArgs&){n.metadata.dpi_scale=2;});
    auto finite=w.events().mouseReleased.listen([&](MouseEventArgs& e){check("recovery UP coordinates remain finite",std::isfinite(e.pos.x)&&std::isfinite(e.pos.y));});
    tool("tc_mouse_click",{{"x",std::numeric_limits<float>::max()*.75f},{"y",0}});
    check("click DPI overflow still sends release and clears ledger",releases==before+1&&w.context().pressedMouseButtons==0&&!w.context().mousePressed&&w.context().mouseButton==-1);
    dpi.disconnect();clearInput(w);n.metadata.dpi_scale=1;
    auto throws=w.events().mousePressed.listen([](MouseEventArgs&){throw 239;});
    tool("tc_mouse_click",{{"x",10},{"y",20}});
    check("throwing press listener still releases",releases==before+2&&w.context().pressedMouseButtons==0&&!w.context().mousePressed);
    throws.disconnect();
    auto rawThrows=w.events().rawEvent.listen([](const sapp_event& e){if(e.type==SAPP_EVENTTYPE_MOUSE_UP)throw 239;});
    tool("tc_mouse_click",{{"x",10},{"y",20}});
    check("throwing release raw listener cannot leave click held",w.context().pressedMouseButtons==0&&w.context().snapshotMouseButtons==0&&!w.context().mousePressed&&w.context().grabbedNode.expired());
}
void clickNewPress(Window& w, Native& n) {
    FixtureWindowScope fixture(w);
    for (bool rawRelease : {false,true}) {
        clearInput(w);internal::pixelPerfectMode()=false;n.metadata.dpi_scale=1;
        auto app=internal::makeConstructedApp<LockedApp>();auto node=std::make_shared<LockedNode>();app->addChild(node);internal::WindowRequestAccess::attachCandidate(w, app);
        {internal::WindowEntryScope entry(w);internal::setupNodeOnce(*app);}
        bool nested=false;
        auto down=[&]{if(!nested){nested=true;n.send(event(SAPP_EVENTTYPE_MOUSE_DOWN,20,30,SAPP_MODIFIER_LMB));}};
        auto release=w.events().mouseReleased.listen([&](MouseEventArgs& e){e.consumed=true;if(!rawRelease)down();},EventPriority::BeforeApp);
        auto raw=w.events().rawEvent.listen([&](const sapp_event& e){if(rawRelease&&e.type==SAPP_EVENTTYPE_MOUSE_UP)down();});
        tool("tc_mouse_click",{{"x",20},{"y",30}});
        check("nested physical DOWN survives click recovery with its grab",nested&&w.context().pressedMouseButtons==1&&w.context().mousePressed&&w.context().mouseButton==0&&w.context().grabbedNode.lock()==node&&w.context().grabbedButton==0);
        release.disconnect();raw.disconnect();
        n.send(event(SAPP_EVENTTYPE_MOUSE_UP,20,30));
        check("later physical UP releases App and Node exactly once",app->releases==1&&node->releases==1&&w.context().pressedMouseButtons==0&&!w.context().mousePressed&&w.context().grabbedNode.expired());
    }
}

// Disabling a nested touch conversion must leave the parent click's UP live.
void disabledTouchInClick(Window& w, Native& n) {
    FixtureWindowScope fixture(w);
    for(bool pixels:{false,true})for(int site=0;site<3;++site) {
        clearInput(w);internal::pixelPerfectMode()=pixels;n.metadata.dpi_scale=1;
        {internal::WindowEntryScope entry(w);setTouchAsMouse(true);}
        struct SettingApp:LockedApp {std::function<void()> touch;void touchPressed(const TouchEventArgs&)override{if(touch)touch();}};
        auto app=internal::makeConstructedApp<SettingApp>();auto a=std::make_shared<LockedNode>();app->addChild(a);internal::WindowRequestAccess::attachCandidate(w, app);
        {internal::WindowEntryScope entry(w);internal::setupNodeOnce(*app);}
        bool nested=false;int typed=0;
        auto disable=[&]{setTouchAsMouse(false);};
        auto raw=w.events().rawEvent.listen([&](const sapp_event& e){if(site==0&&e.type==SAPP_EVENTTYPE_TOUCHES_BEGAN)disable();});
        auto touch=w.events().touchPressed.listen([&](TouchEventArgs&){if(site==1)disable();});
        if(site==2)app->touch=disable;
        auto press=w.events().mousePressed.listen([&](MouseEventArgs&){++typed;if(!nested){nested=true;auto e=event(SAPP_EVENTTYPE_TOUCHES_BEGAN);e.num_touches=1;e.touches[0]={42,20,30,SAPP_ANDROIDTOOLTYPE_FINGER,true};n.send(e);}},EventPriority::BeforeApp);
        tool("tc_mouse_click",{{"x",20},{"y",30}});
        check("disabled nested touch preserves parent click UP without a held button",nested&&typed==1&&a->releases==1&&app->releases==1&&!w.context().mousePressed&&w.context().pressedMouseButtons==0&&!w.context().touchMapper.hasPrimary());
        app->touch={};w.destroyApp();internal::WindowRequestAccess::applyPendingApp(w);
        {internal::WindowEntryScope entry(w);setTouchAsMouse(true);}
    }
}
// A raw failure preserves the old public grab/ledger while retiring a scoped
// click owner. Repeated aborted calls may reuse the same recovery stack address.
void abortedClickOwnership(Window& w, Native& n) {
    FixtureWindowScope fixture(w);
    for(bool pixels:{false,true}) {
        clearInput(w);internal::pixelPerfectMode()=pixels;n.metadata.dpi_scale=1;
        struct AbortNode:LockedNode {bool consume=true;bool onMousePress(const MouseEventArgs&)override{return consume;}};
        auto app=internal::makeConstructedApp<LockedApp>();auto a=std::make_shared<AbortNode>();app->addChild(a);internal::WindowRequestAccess::attachCandidate(w, app);
        {internal::WindowEntryScope entry(w);internal::setupNodeOnce(*app);}
        int phase=0,typed=0;bool nested=false;
        auto count=w.events().mousePressed.listen([&](MouseEventArgs&){++typed;},EventPriority::BeforeApp);
        n.send(event(SAPP_EVENTTYPE_MOUSE_DOWN,20,30,SAPP_MODIFIER_LMB));a->consume=false;
        auto raw=w.events().rawEvent.listen([&](const sapp_event& e){if(e.type==SAPP_EVENTTYPE_MOUSE_DOWN&&(phase==1||e.source==SAPP_EVENTSOURCE_REAL))throw 239;});
        auto after=w.events().mousePressed.listen([&](MouseEventArgs&){if(!nested){nested=true;n.send(event(SAPP_EVENTTYPE_MOUSE_DOWN,120,30,SAPP_MODIFIER_LMB));}},EventPriority::AfterApp);
        tool("tc_mouse_click",{{"x",20},{"y",30}});
        auto intact=[&]{return w.context().grabbedNode.lock()==a&&w.context().pressedMouseButtons==1&&w.context().mousePressed&&a->releases==0&&app->releases==0;};
        check("superseded aborted click keeps base grab and retires scoped owner",intact()&&w.context().mousePressOwners[0]==nullptr);
        phase=1;tool("tc_mouse_click",{{"x",20},{"y",30}});
        check("reused aborted click cannot recover another gesture's grab",intact()&&typed==2);
        raw.disconnect();after.disconnect();n.send(event(SAPP_EVENTTYPE_MOUSE_UP,120,30));
        check("UP after repeated raw failures releases the base grab once",a->releases==1&&app->releases==1&&w.context().pressedMouseButtons==0);
        w.destroyApp();internal::WindowRequestAccess::applyPendingApp(w);
    }
}
// Cross-button cells compare complete observable traces against 8196a698.
// Sites: raw, BeforeApp, App, Node, AfterApp; touch also covers typed/App touch.
void crossButtonMatrix(Window& w, Native& n, bool printRecords=false) {
    FixtureWindowScope fixture(w);
    int cells=0;
    for(bool pixels:{false,true}) for(int outer=0;outer<7;++outer)
    for(int button:{int(SAPP_MOUSEBUTTON_RIGHT),int(SAPP_MOUSEBUTTON_MIDDLE)})
    for(bool down:{false,true}) for(bool throws:{false,true})
    for(bool preHeldOther:{false,true})
    for(int site=0;site<((outer>=2&&outer<=4)?8:5);++site) {
        if(preHeldOther&&(!down||outer>=5))continue;
        clearInput(w);internal::pixelPerfectMode()=pixels;n.metadata.dpi_scale=2;
        {internal::WindowEntryScope entry(w);setTouchAsMouse(true);}
        json trace=json::array();std::function<void()> hook;bool active=false,nested=false,inNested=false;
        auto record=[&](const char* who,const char* kind,const auto& e){if(active)trace.push_back(json::array({who,kind,e.button,e.globalPos.x,e.globalPos.y}));};
        struct CrossApp:App {
            std::function<void(const char*,const MouseEventArgs&)> pointer;
            std::function<void(const char*,const internal::MouseEventRaw&)> motion;
            std::function<void()> touch;
            void mousePressed(const MouseEventArgs& e)override{pointer("press",e);}
            void mouseReleased(const MouseEventArgs& e)override{pointer("release",e);}
            void mouseMoved(const MouseMoveEventArgs& e)override{motion("move",internal::toRaw(e));}
            void mouseDragged(const MouseDragEventArgs& e)override{motion("drag",internal::toRaw(e));}
            void touchPressed(const TouchEventArgs&)override{if(touch)touch();}
            void touchMoved(const TouchEventArgs&)override{if(touch)touch();}
            void touchReleased(const TouchEventArgs&)override{if(touch)touch();}
        };
        struct CrossNode:RectNode {
            std::function<void(const char*,const internal::MouseEventRaw&)> pointer;
            bool consume=true;
            CrossNode(){setSize(80,80);enableEvents();}
            bool onMousePress(const MouseEventArgs& e)override{internal::MouseEventRaw r;r.button=e.button;r.globalPos=e.globalPos;pointer("press",r);return consume;}
            bool onMouseRelease(const MouseEventArgs& e)override{internal::MouseEventRaw r;r.button=e.button;r.globalPos=e.globalPos;pointer("release",r);return false;}
            bool onMouseMove(const MouseMoveEventArgs& e)override{pointer("move",internal::toRaw(e));return false;}
            bool onMouseDrag(const MouseDragEventArgs& e)override{pointer("drag",internal::toRaw(e));return false;}
        };
        auto app=internal::makeConstructedApp<CrossApp>();auto a=std::make_shared<CrossNode>(),b=std::make_shared<CrossNode>();b->setPos(100,0);
        app->addChild(a);app->addChild(b);internal::WindowRequestAccess::attachCandidate(w, app);
        {internal::WindowEntryScope entry(w);internal::setupNodeOnce(*app);}
        auto appHook=[&]{if(active&&!inNested&&site==2&&hook)hook();};
        app->pointer=[&](const char* kind,const MouseEventArgs& e){record("app",kind,e);appHook();};
        app->motion=[&](const char* kind,const internal::MouseEventRaw& e){record("app",kind,e);appHook();};
        app->touch=[&]{if(active&&!inNested&&site==6&&hook)hook();};
        a->pointer=[&](const char* kind,const internal::MouseEventRaw& e){record("A",kind,e);if(active&&!inNested&&site==3&&hook)hook();};
        b->pointer=[&](const char* kind,const internal::MouseEventRaw& e){record("B",kind,e);};
        const float factor=pixels?1:2;const unsigned otherBit=button==SAPP_MOUSEBUTTON_RIGHT?SAPP_MODIFIER_RMB:SAPP_MODIFIER_MMB;
        auto mouse=[&](sapp_event_type type,float x,int btn,unsigned mods){auto e=event(type,x*factor,30*factor,mods);e.mouse_button=sapp_mousebutton(btn);n.send(e);};
        auto touch=[&](sapp_event_type type){auto e=event(type);e.num_touches=1;e.touches[0]={42,20*factor,30*factor,SAPP_ANDROIDTOOLTYPE_FINGER,true};n.send(e);};
        if(outer==1||outer==6)mouse(SAPP_EVENTTYPE_MOUSE_DOWN,20,0,SAPP_MODIFIER_LMB);
        if(outer==3||outer==4)touch(SAPP_EVENTTYPE_TOUCHES_BEGAN);
        if(!down||preHeldOther){
            auto keepGrab=w.events().mousePressed.listen([](MouseEventArgs& e){e.consumed=true;},EventPriority::BeforeApp);
            mouse(SAPP_EVENTTYPE_MOUSE_DOWN,120,button,otherBit|((outer==1||outer==3||outer==4||outer==6)?SAPP_MODIFIER_LMB:0));
        }
        hook=[&]{if(nested)return;nested=true;inNested=true;mouse(down?SAPP_EVENTTYPE_MOUSE_DOWN:SAPP_EVENTTYPE_MOUSE_UP,120,button,SAPP_MODIFIER_LMB|(down?otherBit:0));inNested=false;};
        auto raw=w.events().rawEvent.listen([&](const sapp_event& e){if(active){trace.push_back(json::array({"raw",int(e.type),int(e.mouse_button),e.mouse_x/factor,e.mouse_y/factor}));if(inNested&&throws)throw 239;if(!inNested&&site==0)hook();}});
        auto typed=[&](const char* kind,const auto& e,int where){if(!active)return;record(where==1?"before":"after",kind,e);if(!inNested&&site==where)hook();};
        auto p1=w.events().mousePressed.listen([&](MouseEventArgs& e){typed("press",e,1);},EventPriority::BeforeApp);
        auto r1=w.events().mouseReleased.listen([&](MouseEventArgs& e){typed("release",e,1);},EventPriority::BeforeApp);
        auto m1=w.events().mouseMoved.listen([&](MouseMoveEventArgs& e){internal::MouseEventRaw r=internal::toRaw(e);typed("move",r,1);},EventPriority::BeforeApp);
        auto d1=w.events().mouseDragged.listen([&](MouseDragEventArgs& e){typed("drag",e,1);},EventPriority::BeforeApp);
        auto p2=w.events().mousePressed.listen([&](MouseEventArgs& e){typed("press",e,4);},EventPriority::AfterApp);
        auto r2=w.events().mouseReleased.listen([&](MouseEventArgs& e){typed("release",e,4);},EventPriority::AfterApp);
        auto m2=w.events().mouseMoved.listen([&](MouseMoveEventArgs& e){internal::MouseEventRaw r=internal::toRaw(e);typed("move",r,4);},EventPriority::AfterApp);
        auto d2=w.events().mouseDragged.listen([&](MouseDragEventArgs& e){typed("drag",e,4);},EventPriority::AfterApp);
        auto touchHook=[&](int where){if(active&&!inNested&&site==where)hook();};
        auto t1=w.events().touchPressed.listen([&](TouchEventArgs&){touchHook(5);},EventPriority::BeforeApp);
        auto t2=w.events().touchMoved.listen([&](TouchEventArgs&){touchHook(5);},EventPriority::BeforeApp);
        auto t3=w.events().touchReleased.listen([&](TouchEventArgs&){touchHook(5);},EventPriority::BeforeApp);
        auto t4=w.events().touchPressed.listen([&](TouchEventArgs&){touchHook(7);},EventPriority::AfterApp);
        auto t5=w.events().touchMoved.listen([&](TouchEventArgs&){touchHook(7);},EventPriority::AfterApp);
        auto t6=w.events().touchReleased.listen([&](TouchEventArgs&){touchHook(7);},EventPriority::AfterApp);
        // Permit the requested AfterApp press/release site without changing
        // earlier sites' normal consumption or grab selection.
        if(site==4){a->consume=b->consume=false;}
        active=true;
        if(outer<=1)mouse(outer==0?SAPP_EVENTTYPE_MOUSE_DOWN:SAPP_EVENTTYPE_MOUSE_UP,20,0,SAPP_MODIFIER_LMB|(!down?otherBit:0));
        if(outer>=2&&outer<=4)touch(outer==2?SAPP_EVENTTYPE_TOUCHES_BEGAN:outer==3?SAPP_EVENTTYPE_TOUCHES_ENDED:SAPP_EVENTTYPE_TOUCHES_MOVED);
        if(outer>=5)mouse(SAPP_EVENTTYPE_MOUSE_MOVE,25,0,((outer==6)?SAPP_MODIFIER_LMB:0)|(!down?otherBit:0));
        auto& c=w.context();auto grab=c.grabbedNode.lock();
        char label[180];std::snprintf(label,sizeof(label),"cross root=%d pixels=%d outer=%d button=%d down=%d throws=%d site=%d preHeld=%d",isEarlier(c),pixels,outer,button,down,throws,site,preHeldOther);
        check(label,nested);
        // Raw cross-button DOWN cannot replace the outer left coordinates.
        if(site==0&&(outer==0||outer==1||outer==2||outer==3))check(label,c.mouseX==20&&c.mouseY==30);
        if(site==0&&(outer==0||outer==2))check(label,grab==a&&c.grabbedButton==0&&c.pressedMouseButtons==(1u|((down?(!throws||preHeldOther):throws)?(1u<<button):0u)));
        if(preHeldOther&&outer==4&&site>=1&&site<=3){
            int appDrags=0,hoverMoves=0;
            for(const auto& row:trace){if(row[0]=="app"&&row[1]=="drag")++appDrags;if(row[0]=="A"&&row[1]=="move")++hoverMoves;}
            check(label,appDrags==1&&hoverMoves==1);
        }
        if(site==0&&throws&&down&&outer>=5)check(label,c.mouseX==25&&app!=nullptr&&trace.size()>1);
        if(site==0&&!throws&&down&&outer>=5)check(label,c.mouseX==25&&c.pressedMouseButtons==unsigned(outer==6)&&c.grabbedNode.expired());
        if(printRecords)std::printf("CROSS %s\n",json{{"cell",label},{"nested",nested},{"trace",trace},{"pos",{c.mouseX,c.mouseY}},{"anchor",{c.lastPointerX,c.lastPointerY}},{"held",c.pressedMouseButtons},{"drag",c.mouseButton},{"pressed",c.mousePressed},{"grab",grab==a?1:grab==b?2:0},{"grabButton",c.grabbedButton},{"touch",c.touchMapper.hasPrimary()}}.dump().c_str());
        hook={};app->touch={};app->pointer={};app->motion={};a->pointer={};b->pointer={};w.destroyApp();internal::WindowRequestAccess::applyPendingApp(w);clearInput(w);++cells;
    }
    std::printf("cross-button matrix: root=%d %d cells\n",isEarlier(w.context()),cells);
}
// A consumed UP can leave a grab without a held ledger bit. That grab still
// makes the next motion a continuation of its button's older gesture.
void unheldGrabMotion(Window& w, Native& n) {
    FixtureWindowScope fixture(w);
    for(bool pixels:{false,true}) {
        clearInput(w);internal::pixelPerfectMode()=pixels;n.metadata.dpi_scale=2;
        auto app=internal::makeConstructedApp<LockedApp>();auto a=std::make_shared<LockedNode>(),b=std::make_shared<LockedNode>();
        b->setPos(100,0);app->addChild(a);app->addChild(b);internal::WindowRequestAccess::attachCandidate(w, app);
        {internal::WindowEntryScope entry(w);internal::setupNodeOnce(*app);}
        const float factor=pixels?1:2;
        n.send(event(SAPP_EVENTTYPE_MOUSE_DOWN,20*factor,30*factor,SAPP_MODIFIER_LMB));
        auto consume=w.events().mouseReleased.listen([](MouseEventArgs& e){e.consumed=true;},EventPriority::BeforeApp);
        n.send(event(SAPP_EVENTTYPE_MOUSE_UP,20*factor,30*factor));consume.disconnect();
        check("consumed UP retains base grab without a held bit",w.context().pressedMouseButtons==0&&w.context().grabbedNode.lock()==a);
        bool nested=false;auto raw=w.events().rawEvent.listen([&](const sapp_event& e){if(e.type==SAPP_EVENTTYPE_MOUSE_MOVE&&!nested){nested=true;n.send(event(SAPP_EVENTTYPE_MOUSE_DOWN,120*factor,30*factor,SAPP_MODIFIER_LMB));}});
        n.send(event(SAPP_EVENTTYPE_MOUSE_MOVE,25*factor,30*factor));raw.disconnect();
        check("recovery never refreshes an already concerned grab epoch",nested&&w.context().mouseX==120&&w.context().pressedMouseButtons==1&&w.context().grabbedNode.lock()==b&&app->moves==0&&app->drags==0&&a->releases==0&&b->releases==0);
        n.send(event(SAPP_EVENTTYPE_MOUSE_UP,120*factor,30*factor));
        check("unheld grab supersession only releases the newer press on its UP",a->releases==0&&b->releases==1&&app->releases==1&&w.context().pressedMouseButtons==0);
        w.destroyApp();internal::WindowRequestAccess::applyPendingApp(w);
    }
}
// Full Cartesian matrix: four open/atomic gestures, four newer sources,
// five press sites and both targets; atomic clicks add five release sites.
void newestPressMatrix(Window& w, Native& n) {
    FixtureWindowScope fixture(w);
    enum Source { Click, Drag, NativeDown, Touch };
    int cells=0;
    for(bool pixels:{false,true})for(int older=0;older<4;++older)for(int newer=0;newer<4;++newer)
    for(int site=0;site<(older==Click?10:5);++site)for(bool different:{false,true}) {
        if(!isEarlier(w.context())&&(older<2||newer==3))continue;
        clearInput(w);internal::pixelPerfectMode()=pixels;n.metadata.dpi_scale=2;
        const float factor=pixels?1:2;
        auto send=[&](sapp_event e){e.mouse_x*=factor;e.mouse_y*=factor;return n.send(e);};
        struct MatrixNode:LockedNode {
            std::function<void()> press,release;
            bool consumePress=true,consumeRelease=true;
            bool onMousePress(const MouseEventArgs&)override {if(press)press();return consumePress;}
            bool onMouseRelease(const MouseEventArgs&)override {++releases;if(release)release();return consumeRelease;}
        };
        struct MatrixApp:LockedApp {
            std::function<void()> press,release;
            void mousePressed(const MouseEventArgs&)override {if(press)press();}
            void mouseReleased(const MouseEventArgs& e)override {LockedApp::mouseReleased(e);if(release)release();}
        };
        auto app=internal::makeConstructedApp<MatrixApp>();
        auto a=std::make_shared<MatrixNode>(),b=std::make_shared<MatrixNode>();
        b->setPos(100,0);app->addChild(a);app->addChild(b);internal::WindowRequestAccess::attachCandidate(w, app);
        {internal::WindowEntryScope entry(w);internal::setupNodeOnce(*app);}
        auto target=different?b:a;const float x=different?120:20;
        bool nested=false,insideHeld=false,insideGrab=false,insidePrimary=false;
        int beforeA=0,beforeB=0,beforeApp=0;uint64_t newestEpoch=0;
        auto touch=[&](sapp_event_type type,uintptr_t id,float px){
            auto e=event(type);e.num_touches=1;e.touches[0]={id,px*factor,30*factor,SAPP_ANDROIDTOOLTYPE_FINGER,true};n.send(e);
        };
        auto down=[&]{
            if(nested)return;nested=true;
            beforeA=a->releases;beforeB=b->releases;beforeApp=app->releases;
            a->consumePress=b->consumePress=a->consumeRelease=b->consumeRelease=true;
            if(newer==0)send(event(SAPP_EVENTTYPE_MOUSE_DOWN,x,30,SAPP_MODIFIER_LMB));
            if(newer==1){auto e=event(SAPP_EVENTTYPE_MOUSE_DOWN,x*factor,30*factor,SAPP_MODIFIER_LMB);check("matrix injected newer DOWN admitted",n.operations.injectEvent({n.win_id},&n,&e));}
            if(newer==2)touch(SAPP_EVENTTYPE_TOUCHES_BEGAN,42,x);
            if(newer==3)tool("tc_mouse_click",{{"x",x},{"y",30}});
            auto& c=w.context();newestEpoch=c.mousePressGenerations[0];
            insideHeld=c.mousePressed;insideGrab=c.grabbedNode.lock()==target;insidePrimary=c.touchMapper.hasPrimary();

        };
        const bool releaseSite=site>=5;const int point=site%5;
        auto raw=w.events().rawEvent.listen([&](const sapp_event& e){
            const bool press=e.type==(older==Touch?SAPP_EVENTTYPE_TOUCHES_BEGAN:SAPP_EVENTTYPE_MOUSE_DOWN);
            if(point==0&&(releaseSite?e.type==SAPP_EVENTTYPE_MOUSE_UP:press))down();
        });
        auto before=w.events().mousePressed.listen([&](MouseEventArgs&){if(!releaseSite&&point==1)down();},EventPriority::BeforeApp);
        auto beforeUp=w.events().mouseReleased.listen([&](MouseEventArgs&){if(releaseSite&&point==1)down();},EventPriority::BeforeApp);
        auto after=w.events().mousePressed.listen([&](MouseEventArgs&){if(!releaseSite&&point==4)down();},EventPriority::AfterApp);
        auto afterUp=w.events().mouseReleased.listen([&](MouseEventArgs&){if(releaseSite&&point==4)down();},EventPriority::AfterApp);
        if(point==2){if(releaseSite)app->release=down;else app->press=down;}
        if(point==3){if(releaseSite)a->release=down;else a->press=down;}
        // A consuming tree intentionally prevents AfterApp delivery. Let only
        // the older event bubble in these cells, then require the newer grab.
        if(point==4){if(releaseSite)a->consumeRelease=false;else a->consumePress=false;}
        if(older==Click)tool("tc_mouse_click",{{"x",20},{"y",30}});
        if(older==Drag)tool("tc_mouse_press",{{"x",20},{"y",30}});
        if(older==NativeDown)send(event(SAPP_EVENTTYPE_MOUSE_DOWN,20,30,SAPP_MODIFIER_LMB));
        if(older==Touch)touch(SAPP_EVENTTYPE_TOUCHES_BEGAN,41,20);
        char label[220];
        auto cellCheck=[&](const char* name,bool value){std::snprintf(label,sizeof(label),"matrix root=%d pixels=%d older=%d newer=%d site=%d different=%d: %s",isEarlier(w.context()),pixels,older,newer,site,different,name);check(label,value);};
        auto& c=w.context();const bool held=newer!=3;
        cellCheck("reentry ran and newest epoch retained",nested&&c.mousePressGenerations[0]==newestEpoch);
        cellCheck("newest owns ledger pressed state provenance",insideHeld==held&&c.pressedMouseButtons==(held?1u:0u)&&c.snapshotMouseButtons==0&&c.mousePressed==held&&c.mouseButton==(held?0:-1)&&c.mousePressOwners[0]==nullptr);
        cellCheck("newest owns grab",insideGrab==held&&(held?c.grabbedNode.lock()==target:c.grabbedNode.expired())&&c.grabbedButton==(held?0:-1));
        const bool primary = newer==2 || (older==Touch && site>0);
        cellCheck("touch identity preserves base physical DOWN semantics",insidePrimary==primary&&c.touchMapper.hasPrimary()==primary);
        cellCheck("absolute releases include only observed old UP and newer click UP",
            a->releases==beforeA+(newer==3&&!different)&&b->releases==beforeB+(newer==3&&different)&&app->releases==beforeApp+(newer==3));
        cellCheck("anchor follows newest pointer",c.mouseX==x&&c.lastPointerX==x&&c.mouseY==30&&c.lastPointerY==30);
        raw.disconnect();before.disconnect();beforeUp.disconnect();after.disconnect();afterUp.disconnect();
        app->press=app->release=a->press=a->release=b->press=b->release={};
        const int ad=a->drags,bd=b->drags,ar=a->releases,br=b->releases,apr=app->releases,apd=app->drags;
        bool dragPressed=false;auto drag=w.events().mouseDragged.listen([&](MouseDragEventArgs&){dragPressed=c.mousePressed;},EventPriority::BeforeApp);
        if(newer==2)touch(SAPP_EVENTTYPE_TOUCHES_MOVED,42,x+5);
        else send(event(SAPP_EVENTTYPE_MOUSE_MOVE,x+5,35,held?SAPP_MODIFIER_LMB:0));
        cellCheck("later motion drags newest Node exactly once with pressed state",a->drags==ad+(held&&!different)&&b->drags==bd+(held&&different)&&app->drags==apd+held&&dragPressed==held);
        if(newer==2){touch(SAPP_EVENTTYPE_TOUCHES_ENDED,42,x+5);touch(SAPP_EVENTTYPE_TOUCHES_ENDED,42,x+5);}
        else {send(event(SAPP_EVENTTYPE_MOUSE_UP,x+5,35));send(event(SAPP_EVENTTYPE_MOUSE_UP,x+5,35));}
        cellCheck("later release reaches newest Node exactly once",a->releases==ar+(held&&!different)&&b->releases==br+(held&&different)&&app->releases==apr+held);
        cellCheck("no gesture remains held",c.pressedMouseButtons==0&&c.snapshotMouseButtons==0&&!c.mousePressed&&c.mouseButton==-1&&c.grabbedNode.expired()&&c.grabbedButton==-1&&c.touchMapper.hasPrimary()==(primary&&newer!=2));
        c.touchMapper.reset();
        w.destroyApp();internal::WindowRequestAccess::applyPendingApp(w);++cells;
    }
    check("generated press matrix includes both coordinate modes",cells==(isEarlier(w.context())?400:120));
    std::printf("newest-press matrix: root=%d %d cells\n",isEarlier(w.context()),cells);
}

// Generated axes: both roots (caller), both coordinate modes, motion callback
// sites, touch MOVE/END reentry, callback-visible settings and base grab rules.
void continuationMatrix(Window& w, Native& n, int regressionFilter=0) {
    FixtureWindowScope fixture(w);
    int cells=0;
    struct TestNode:LockedNode {
        std::function<void()> drag;bool consume=true;
        bool onMouseDrag(const MouseDragEventArgs& e)override {LockedNode::onMouseDrag(e);if(drag)drag();return consume;}
    };
    struct TestApp:LockedApp {
        std::function<void()> drag,touch,move;
        void mouseDragged(const MouseDragEventArgs& e)override {LockedApp::mouseDragged(e);if(drag)drag();}
        void touchPressed(const TouchEventArgs&)override {if(touch)touch();}
        void touchMoved(const TouchEventArgs&)override {if(move)move();}
    };
    for(bool pixels:{false,true})for(int operation=0;operation<4;++operation)
    for(int site=0;site<(operation==0?5:operation==1?4:operation==2?6:7);++site) {
        if(regressionFilter&&(operation==0||(regressionFilter==1&&operation==3)))continue;
        clearInput(w);internal::pixelPerfectMode()=pixels;n.metadata.dpi_scale=2;
        {internal::WindowEntryScope entry(w);setTouchAsMouse(!(operation==2&&site>=3));}
        auto app=internal::makeConstructedApp<TestApp>();auto a=std::make_shared<TestNode>(),b=std::make_shared<TestNode>();
        b->setPos(100,0);app->addChild(a);app->addChild(b);internal::WindowRequestAccess::attachCandidate(w, app);
        {internal::WindowEntryScope entry(w);internal::setupNodeOnce(*app);}
        const float scale=pixels?1:2;
        auto mouse=[&](sapp_event_type type,float x){n.send(event(type,x*scale,30*scale,type==SAPP_EVENTTYPE_MOUSE_UP?0:SAPP_MODIFIER_LMB));};
        auto touch=[&](sapp_event_type type){auto e=event(type);e.num_touches=1;e.touches[0]={42,20*scale,30*scale,SAPP_ANDROIDTOOLTYPE_FINGER,true};n.send(e);};
        bool nested=false;
        auto reenter=[&]{if(nested)return;nested=true;
            if(operation==0)mouse(SAPP_EVENTTYPE_MOUSE_DOWN,120);
            if(operation==1)touch(SAPP_EVENTTYPE_TOUCHES_ENDED);
            if(operation==2){internal::WindowEntryScope entry(w);setTouchAsMouse(site>=3);}
        };
        auto raw=w.events().rawEvent.listen([&](const sapp_event& e){
            if((site==0||(operation==2&&site==3))&&((operation==0&&e.type==SAPP_EVENTTYPE_MOUSE_MOVE)||(operation==1&&e.type==SAPP_EVENTTYPE_TOUCHES_MOVED)||(operation==2&&e.type==SAPP_EVENTTYPE_TOUCHES_BEGAN)))reenter();
            if(operation==3&&(site==2||site==5)&&nested&&e.type==SAPP_EVENTTYPE_MOUSE_DOWN)throw 239;
        });
        auto drag=w.events().mouseDragged.listen([&](MouseDragEventArgs&){if(operation==0&&site==1)reenter();},EventPriority::BeforeApp);
        auto move=w.events().touchMoved.listen([&](TouchEventArgs&){if(operation==1&&site==1)reenter();});
        auto press=w.events().touchPressed.listen([&](TouchEventArgs&){if(operation==2&&site%3==1)reenter();});
        auto end=w.events().touchReleased.listen([&](TouchEventArgs&){if(operation==1&&site==3&&!nested){nested=true;touch(SAPP_EVENTTYPE_TOUCHES_ENDED);}});
        auto consume=w.events().mousePressed.listen([&](MouseEventArgs& e){if(operation==3&&(site==0||site==4)&&nested)e.consumed=true;},EventPriority::BeforeApp);
        if(operation==0&&site==2)app->drag=reenter;
        if(operation==0&&site==3)a->drag=reenter;
        auto after=w.events().mouseDragged.listen([&](MouseDragEventArgs&){if(operation==0&&site==4)reenter();},EventPriority::AfterApp);
        if(operation==0&&site==4)a->consume=false;
        if(operation==1&&site==2)app->move=reenter;
        if(operation==2&&site%3==2)app->touch=reenter;
        if(operation==3&&site>=3)touch(SAPP_EVENTTYPE_TOUCHES_BEGAN);else if(operation!=2)mouse(SAPP_EVENTTYPE_MOUSE_DOWN,20);
        if(operation==1){clearInput(w);touch(SAPP_EVENTTYPE_TOUCHES_BEGAN);}
        if(operation==0)mouse(SAPP_EVENTTYPE_MOUSE_MOVE,25);
        if(operation==1)touch(site==3?SAPP_EVENTTYPE_TOUCHES_ENDED:SAPP_EVENTTYPE_TOUCHES_MOVED);
        if(operation==2)touch(SAPP_EVENTTYPE_TOUCHES_BEGAN);
        if(operation==3){nested=true;mouse(SAPP_EVENTTYPE_MOUSE_DOWN,(site==1||site==6)?320:120);}
        char label[180];auto cell=[&](const char* name,bool ok){std::snprintf(label,sizeof(label),"continuation root=%d pixels=%d operation=%d site=%d: %s",isEarlier(w.context()),pixels,operation,site,name);check(label,ok);};
        auto& c=w.context();
        if(operation==0){cell("new grab and anchor survive old motion",nested&&c.grabbedNode.lock()==b&&c.mouseX==120&&b->drags==0&&a->moves==(site==4)&&app->drags==(site>=2));mouse(SAPP_EVENTTYPE_MOUSE_UP,120);cell("only newest release",a->releases==0&&b->releases==1&&app->releases==1);}
        if(operation==1){cell("ended finger never resurrected or dragged",nested&&!c.touchMapper.hasPrimary()&&c.pressedMouseButtons==0&&app->drags==0&&a->drags==0&&a->releases==1&&app->releases==1);touch(SAPP_EVENTTYPE_TOUCHES_ENDED);cell("duplicate END has no release",a->releases==1&&app->releases==1);}
        if(operation==2){const bool enabled=site>=3;cell("callback setting controls current mouse press",nested&&c.touchMapper.hasPrimary()==enabled&&c.pressedMouseButtons==unsigned(enabled)&&c.grabbedNode.expired()==!enabled);touch(SAPP_EVENTTYPE_TOUCHES_ENDED);cell("setting change never leaves held button",c.pressedMouseButtons==0&&!c.mousePressed&&app->releases==int(enabled));}
        if(operation==3){cell("admission preserves base grab and touch identity",c.grabbedNode.lock()==(site==3?b:a)&&c.touchMapper.hasPrimary()==(site>=3)&&a->releases==0&&b->releases==0);mouse(SAPP_EVENTTYPE_MOUSE_UP,120);cell("later UP follows base grab",a->releases==(site!=3)&&b->releases==(site==3)&&app->releases==1);}
        app->drag=app->touch=app->move=a->drag=b->drag={};w.destroyApp();internal::WindowRequestAccess::applyPendingApp(w);clearInput(w);
        {internal::WindowEntryScope entry(w);setTouchAsMouse(true);}++cells;
    }
    std::printf("continuation matrix: root=%d %d cells\n",isEarlier(w.context()),cells);
}
void supersessionObservers(Window& w, Native& n) {
    FixtureWindowScope fixture(w);
    for(bool pixels:{false,true})for(int site=0;site<5;++site) {
        clearInput(w);internal::pixelPerfectMode()=pixels;n.metadata.dpi_scale=2;
        struct ObserverNode:LockedNode {
            std::function<void()> release;
            bool onMouseRelease(const MouseEventArgs&)override{++releases;if(release)release();return false;}
        };
        struct ObserverApp:LockedApp {
            std::function<void()> release;
            void mouseReleased(const MouseEventArgs& e)override{LockedApp::mouseReleased(e);if(release)release();}
        };
        auto app=internal::makeConstructedApp<ObserverApp>();auto a=std::make_shared<ObserverNode>(),b=std::make_shared<ObserverNode>();
        b->setPos(100,0);app->addChild(a);app->addChild(b);internal::WindowRequestAccess::attachCandidate(w, app);
        {internal::WindowEntryScope entry(w);internal::setupNodeOnce(*app);}
        const float factor=pixels?1:2;bool nested=false;int rawTail=0,before=0,after=0;
        auto down=[&]{if(nested)return;nested=true;n.send(event(SAPP_EVENTTYPE_MOUSE_DOWN,120*factor,30*factor,SAPP_MODIFIER_LMB));};
        auto raw=w.events().rawEvent.listen([&](const sapp_event& e){if(site==0&&e.type==SAPP_EVENTTYPE_MOUSE_UP)down();});
        auto tail=w.events().rawEvent.listen([&](const sapp_event& e){if(e.type==SAPP_EVENTTYPE_MOUSE_UP)++rawTail;});
        auto early=w.events().mouseReleased.listen([&](MouseEventArgs&){++before;if(site==1)down();},EventPriority::BeforeApp);
        auto late=w.events().mouseReleased.listen([&](MouseEventArgs&){++after;if(site==4)down();},EventPriority::AfterApp);
        if(site==2)app->release=down;if(site==3)a->release=down;
        n.send(event(SAPP_EVENTTYPE_MOUSE_DOWN,20*factor,30*factor,SAPP_MODIFIER_LMB));
        check("press itself sends no release or cancel",a->releases==0&&b->releases==0&&app->releases==0&&rawTail==0&&before==0&&after==0);
        n.send(event(SAPP_EVENTTYPE_MOUSE_UP,20*factor,30*factor));
        check("partial UP keeps delivered observations and normal plain listeners",nested&&rawTail==1&&before==(site!=0)&&after==(site!=0)&&app->releases==(site>=2)&&a->releases==(site>=3)&&b->releases==0&&w.context().grabbedNode.lock()==b);
        app->release=a->release={};
        n.send(event(SAPP_EVENTTYPE_MOUSE_UP,120*factor,30*factor));
        check("later UP releases the surviving new grab once",b->releases==1&&app->releases==(site>=2)+1&&w.context().grabbedNode.expired());
        w.destroyApp();internal::WindowRequestAccess::applyPendingApp(w);
    }
    std::printf("supersession observer matrix: root=%d 10 cells\n",isEarlier(w.context()));
}
void touchExtremes(Window& w, Native& n) {
    FixtureWindowScope fixture(w);
    clearInput(w);internal::pixelPerfectMode()=true;n.metadata.dpi_scale=.5f;
    int raw=0,touch=0,mouse=0;Vec2 pos;
    auto r=w.events().rawEvent.listen([&](const sapp_event&){++raw;});
    auto t=w.events().touchPressed.listen([&](TouchEventArgs&){++touch;});
    auto p=w.events().mousePressed.listen([&](MouseEventArgs& e){++mouse;pos=e.pos;});
    auto e=event(SAPP_EVENTTYPE_TOUCHES_BEGAN);e.num_touches=1;
    const float extreme=std::numeric_limits<float>::max()*.75f;
    e.touches[0]={42,extreme,20,SAPP_ANDROIDTOOLTYPE_FINGER,true};n.send(e);
    check("large pixel touch keeps a finite logical intermediate",raw==1&&touch==1&&mouse==1&&pos==Vec2(extreme,20)&&w.context().touchMapper.hasPrimary()&&w.context().pressedMouseButtons==1&&w.context().pointerValid);
    clearInput(w);n.metadata.dpi_scale=1.4f;raw=touch=mouse=0;
    e.touches[0].pos_x=std::numeric_limits<float>::max();
    Vec2 rawPos,typedPos;
    auto boundaryRaw=w.events().rawEvent.listen([&](const sapp_event& packet){rawPos=Vec2(packet.touches[0].pos_x,packet.touches[0].pos_y);});
    auto boundaryTyped=w.events().touchPressed.listen([&](TouchEventArgs& packet){typedPos=Vec2(packet.touches[0].x,packet.touches[0].y);});
    n.send(e);
    check("FLT_MAX pixel touch at fractional DPI preserves raw typed and mouse input",raw==1&&touch==1&&mouse==1&&rawPos==Vec2(e.touches[0].pos_x,20)&&typedPos==rawPos&&pos==rawPos&&w.context().lastPointerX==e.touches[0].pos_x&&w.context().touchMapper.hasPrimary()&&w.context().pressedMouseButtons==1);
    boundaryRaw.disconnect();boundaryTyped.disconnect();
    clearInput(w);internal::pixelPerfectMode()=false;n.metadata.dpi_scale=.5f;raw=touch=mouse=0;
    n.send(e);
    check("unrepresentable touch position rejects before callbacks and state",raw==0&&touch==0&&mouse==0&&!w.context().pointerValid&&!w.context().touchMapper.hasPrimary()&&w.context().pressedMouseButtons==0);
    clearInput(w);internal::pixelPerfectMode()=true;n.metadata.dpi_scale=1.4f;
    n.send(event(SAPP_EVENTTYPE_MOUSE_DOWN,e.touches[0].pos_x,20));
    check("FLT_MAX pixel mouse at fractional DPI preserves input",pos==Vec2(e.touches[0].pos_x,20)&&w.context().lastPointerX==e.touches[0].pos_x);
    auto relative=event(SAPP_EVENTTYPE_MOUSE_MOVE,e.touches[0].pos_x,20,SAPP_MODIFIER_LMB);relative.pointer_locked=true;relative.mouse_dx=e.touches[0].pos_x;
    Vec2 relativeDelta;auto drag=w.events().mouseDragged.listen([&](MouseDragEventArgs& packet){relativeDelta=packet.delta;});
    n.send(relative);
    check("FLT_MAX pixel locked relative at fractional DPI preserves input",relativeDelta==Vec2(e.touches[0].pos_x,0)&&w.context().lastPointerX==e.touches[0].pos_x);
    clearInput(w);internal::pixelPerfectMode()=false;n.metadata.dpi_scale=.5f;
    n.send(event(SAPP_EVENTTYPE_MOUSE_DOWN,10,20));raw=touch=mouse=0;
    const float anchor=w.context().lastPointerX;n.send(relative);
    check("unrepresentable locked relative rejects before callbacks and state",raw==0&&touch==0&&mouse==0&&w.context().lastPointerX==anchor&&w.context().pressedMouseButtons==1);
    drag.disconnect();e.touches[0].pos_x=extreme;internal::pixelPerfectMode()=true;
    // The mapped primary action, including its old anchor, must pass before any callback.
    clearInput(w);n.metadata.dpi_scale=1;
    n.send(event(SAPP_EVENTTYPE_MOUSE_MOVE,-extreme,20));raw=touch=mouse=0;
    n.send(e);
    check("rejected touch delta leaves callbacks mapper ledger and anchor unchanged",raw==0&&touch==0&&mouse==0&&!w.context().touchMapper.hasPrimary()&&w.context().pressedMouseButtons==0&&w.context().lastPointerX==-extreme&&w.context().pointerBasis==1);
    // Even a finite packet can overflow a previous anchor when re-expressed at a new DPI.
    clearInput(w);n.send(event(SAPP_EVENTTYPE_MOUSE_MOVE,extreme,20));n.metadata.dpi_scale=2;raw=touch=mouse=0;e.touches[0].pos_x=1;
    n.send(e);
    check("rejected touch anchor keeps mapper ledger and basis unchanged",raw==0&&touch==0&&mouse==0&&!w.context().touchMapper.hasPrimary()&&w.context().pressedMouseButtons==0&&w.context().lastPointerX==extreme&&w.context().pointerBasis==1);
    clearInput(w);internal::pixelPerfectMode()=false;n.metadata.dpi_scale=std::numeric_limits<float>::denorm_min();raw=touch=mouse=0;
    e.touches[0].pos_x=e.touches[0].pos_y=n.metadata.dpi_scale;n.send(e);
    check("tiny DPI touch conversion avoids reciprocal overflow",raw==1&&touch==1&&mouse==1&&pos==Vec2(1,1));
    clearInput(w);n.send(event(SAPP_EVENTTYPE_MOUSE_DOWN,n.metadata.dpi_scale,n.metadata.dpi_scale));
    check("tiny DPI mouse conversion avoids reciprocal overflow",pos==Vec2(1,1)&&w.context().lastPointerX==1);
    n.metadata.dpi_scale=1;clearInput(w);
}

void finiteExtremes(Window& w, Native& n) {
    FixtureWindowScope fixture(w);
    clearInput(w);internal::pixelPerfectMode()=true;n.metadata.dpi_scale=1;
    auto e=event(SAPP_EVENTTYPE_MOUSE_MOVE,std::numeric_limits<float>::max(),-std::numeric_limits<float>::max());n.send(e);
    check("finite extreme pointer positions retain float event range",w.context().mouseX==e.mouse_x&&w.context().mouseY==e.mouse_y);
    int raw=0,typed=0;bool finite=true;
    auto r=w.events().rawEvent.listen([&](const sapp_event&){++raw;});
    auto m=w.events().mouseMoved.listen([&](MouseMoveEventArgs& e){++typed;finite=finite&&std::isfinite(e.delta.x)&&std::isfinite(e.delta.y);});
    const auto previous=w.context().lastPointerX;
    e.mouse_x=-e.mouse_x;e.mouse_y=-e.mouse_y;e.source=SAPP_EVENTSOURCE_REPLAYED;
    n.operations.injectEvent({n.win_id},&n,&e);
    check("opposite finite extremes never deliver infinite delta",finite);
    check("derived overflow rejects before raw or anchor mutation",raw==0&&typed==0&&w.context().lastPointerX==previous);
    clearInput(w);n.metadata.dpi_scale=.5f;
    e.mouse_x=std::numeric_limits<float>::max();e.mouse_y=0;internal::pixelPerfectMode()=false;
    n.operations.injectEvent({n.win_id},&n,&e);
    check("converted position overflow rejects before raw and anchor",raw==0&&typed==0&&!w.context().pointerValid);
    clearInput(w);n.metadata.dpi_scale=1;internal::pixelPerfectMode()=true;
    n.send(event(SAPP_EVENTTYPE_MOUSE_MOVE,std::numeric_limits<float>::max()*.75f));
    raw=typed=0;n.metadata.dpi_scale=2;
    n.send(event(SAPP_EVENTTYPE_MOUSE_MOVE,1));
    check("DPI anchor overflow rejects without mutating basis",raw==0&&typed==0&&w.context().pointerBasis==1);
    internal::pixelPerfectMode()=false;
}
void nestedPointerBasis(Window& w, Native& n) {
    FixtureWindowScope fixture(w);
    clearInput(w);internal::pixelPerfectMode()=true;n.metadata.dpi_scale=1;
    std::vector<float> deltas;
    auto move=w.events().mouseMoved.listen([&](MouseMoveEventArgs& e){deltas.push_back(e.delta.x);});
    n.send(event(SAPP_EVENTTYPE_MOUSE_MOVE,100,40));
    bool nested=false;
    auto raw=w.events().rawEvent.listen([&](const sapp_event& e){if(e.type==SAPP_EVENTTYPE_MOUSE_MOVE&&!nested){nested=true;n.metadata.dpi_scale=2;n.send(event(SAPP_EVENTTYPE_MOUSE_MOVE,220,80));}});
    n.send(event(SAPP_EVENTTYPE_MOUSE_MOVE,120,40));raw.disconnect();n.send(event(SAPP_EVENTTYPE_MOUSE_MOVE,260,80));
    check("nested raw input keeps each event coordinate basis",deltas==std::vector<float>{0,20,10,20});
    clearInput(w);n.metadata.dpi_scale=1;n.send(event(SAPP_EVENTTYPE_MOUSE_DOWN,20,30));
    bool reentered=false;int releases=0;
    auto release=w.events().mouseReleased.listen([&](MouseEventArgs& e){++releases;check("reentrant release never publishes an infinite anchor",std::isfinite(e.pos.x));});
    auto extreme=w.events().rawEvent.listen([&](const sapp_event& e){
        if(e.type==SAPP_EVENTTYPE_MOUSE_MOVE&&!reentered){
            reentered=true;n.metadata.dpi_scale=.5f;
            n.send(event(SAPP_EVENTTYPE_MOUSE_MOVE,std::numeric_limits<float>::max()*.75f,30,SAPP_MODIFIER_LMB));
        }
    });
    n.send(event(SAPP_EVENTTYPE_MOUSE_MOVE,30,40));
    check("raw reentry cannot overflow the recovery anchor before position validation",releases==0&&w.context().pointerBasis==.5f&&std::isfinite(w.context().lastPointerX));
    extreme.disconnect();clearInput(w);
    internal::pixelPerfectMode()=false;
}
void sourceParity(Window& w, Native& n) {
    FixtureWindowScope fixture(w);
    std::vector<std::string> reference;
    for (auto source : {SAPP_EVENTSOURCE_REAL,SAPP_EVENTSOURCE_INJECTED,SAPP_EVENTSOURCE_REPLAYED}) {
        clearInput(w); n.metadata.dpi_scale=1;
        std::vector<std::string> trace;
        auto raw=w.events().rawEvent.listen([&](const sapp_event& e){check("real injected replay source tags",e.source==source);trace.push_back("raw:"+std::to_string(e.type));});
        auto point=[&](const char* name,auto& e){trace.push_back(std::string(name)+":"+std::to_string(e.pos.x)+":"+std::to_string(e.pos.y));};
        auto press=w.events().mousePressed.listen([&](MouseEventArgs& e){point("press",e);});
        auto release=w.events().mouseReleased.listen([&](MouseEventArgs& e){point("release",e);});
        auto drag=w.events().mouseDragged.listen([&](MouseDragEventArgs& e){point("drag",e);trace.push_back(std::to_string(e.delta.x));});
        auto move=w.events().mouseMoved.listen([&](MouseMoveEventArgs& e){point("move",e);trace.push_back(std::to_string(e.delta.x));});
        auto key=w.events().keyPressed.listen([&](KeyEventArgs& e){trace.push_back("key:"+std::to_string(e.key));});
        for(auto e:{event(SAPP_EVENTTYPE_MOUSE_MOVE,100,40),event(SAPP_EVENTTYPE_MOUSE_DOWN,100,40),event(SAPP_EVENTTYPE_MOUSE_MOVE,120,60,SAPP_MODIFIER_LMB),event(SAPP_EVENTTYPE_MOUSE_LEAVE),event(SAPP_EVENTTYPE_MOUSE_ENTER,140,80,SAPP_MODIFIER_LMB),event(SAPP_EVENTTYPE_MOUSE_UP,150,90),event(SAPP_EVENTTYPE_KEY_DOWN),event(SAPP_EVENTTYPE_CHAR)}) {
            e.source=source;
            if(source==SAPP_EVENTSOURCE_REAL)n.send(e);else check("injected or replay trace admitted",n.operations.injectEvent({n.win_id},&n,&e));
        }
        if(source==SAPP_EVENTSOURCE_REAL)reference=trace;else check("same real injected replay trace produces identical typed data and order",trace==reference);
    }
}
void touchIsolation(Window& a, Native& an, Window& b, Native& bn) {
    clearInput(a);clearInput(b);an.metadata.dpi_scale=bn.metadata.dpi_scale=1;
    int ap=0,bp=0,ad=0,bd=0;
    auto pa=a.events().mousePressed.listen([&](MouseEventArgs&){++ap;});auto pb=b.events().mousePressed.listen([&](MouseEventArgs&){++bp;});
    auto da=a.events().mouseDragged.listen([&](MouseDragEventArgs&){++ad;});auto db=b.events().mouseDragged.listen([&](MouseDragEventArgs&){++bd;});
    auto e=event(SAPP_EVENTTYPE_TOUCHES_BEGAN);e.num_touches=1;e.touches[0]={42,10,20,SAPP_ANDROIDTOOLTYPE_FINGER,true};an.send(e);e.touches[0].pos_x=200;bn.send(e);
    e.type=SAPP_EVENTTYPE_TOUCHES_MOVED;an.send(e);bn.send(e);e.type=SAPP_EVENTTYPE_TOUCHES_ENDED;an.send(e);e.type=SAPP_EVENTTYPE_TOUCHES_MOVED;bn.send(e);
    check("interleaved touches retain each Window primary identity",ap==1&&bp==1&&ad==1&&bd==2);
    e.type=SAPP_EVENTTYPE_TOUCHES_ENDED;bn.send(e);
}
void closes(Window& w, Native& n) {
    FixtureWindowScope fixture(w);
    for(bool synced:{false,true}) {
        for(bool fromRender:{false,true}) {
            n.closed=false;n.metadata.iconified=n.metadata.occluded=false;
            internal::currentWindowCtx()=&w.context();
            if (synced) w.setFps(VSYNC); else w.setIndependentFps(VSYNC,VSYNC);
            int updates=0,draws=0,presents=0,after=0,begins=0,render=0;bool acquired=false,shown=false;
            struct CloseApp : App {
                Native& native; bool closeInUpdate; int draws=0;
                CloseApp(Native& native,bool closeInUpdate):native(native),closeInUpdate(closeInUpdate){}
                void update() override { if(closeInUpdate) native.closed=true; }
                void draw() override { ++draws; }
            };
            auto app=internal::makeConstructedApp<CloseApp>(n,!fromRender);internal::WindowRequestAccess::attachCandidate(w, app);internal::WindowRequestAccess::applyPendingApp(w);
            w.context().appEventListeners.at(1)=internal::FrameworkEventAccess::listen(w.events().draw,[app]{app->draw();},EventPriority::App);
            auto update=w.events().update.listen([&]{++updates;});
            auto draw=w.events().draw.listen([&]{++draws;});
            auto onRender=w.events().onRender.listen([&]{++render;n.closed=true;});
            auto finished=w.events().afterFrame.listen([&]{++after;});
            const internal::WindowTickPresentation presentation{
                [&]{++begins;acquired=true;},[&]{w.events().onRender.notify();if(internal::currentWindowCloseRecorded())return true;++presents;shown=acquired;return true;},[]{}};
            auto now=std::chrono::steady_clock::now()+std::chrono::hours(10);
            bool result=internal::enterWindowTick(w,{n.win_id},now,.016,&presentation);
            check("direct native close cancels frame completion",!result&&updates==1&&presents==0&&after==0&&!shown);
            check("close from update skips draw and render",fromRender||(draws==0&&render==0));
            check("independent update close acquires no new drawable",synced||fromRender||begins==0);
            check("close from onRender never presents acquired drawable",!fromRender||(draws==1&&render==1&&acquired));
            check("App virtual draw agrees with close admission",app->draws==(fromRender?1:0));
            n.closed=false;w.destroyApp();internal::WindowRequestAccess::applyPendingApp(w);
        }
    }
    n.closed=false;w.setIndependentFps(VSYNC,VSYNC);
    int postCloseDraws=0,postCloseFrames=0;
    auto close=w.events().update.listen([&]{internal::WindowRequestAccess::unbind(w);});
    auto draw=w.events().draw.listen([&]{++postCloseDraws;});auto after=w.events().afterFrame.listen([&]{++postCloseFrames;});
    const internal::WindowTickPresentation p{[]{},[]{return true;},[]{}};
    const bool completed=internal::enterWindowTick(w,{n.win_id},std::chrono::steady_clock::now()+std::chrono::hours(20),.016,&p);
    check("native close unpublication still cancels the receiving frame",!completed&&postCloseDraws==0&&postCloseFrames==0);
    internal::bindWindow(w,{n.win_id},n.operations,&n);
    n.closed=false;internal::currentWindowCtx()=nullptr;
}
}
extern "C" void* __real__Znwm(size_t);extern "C" void* __real__Znam(size_t);
extern "C" void* __wrap__Znwm(size_t s){return __real__Znwm(s);}extern "C" void* __wrap__Znam(size_t s){return __real__Znam(s);}
extern "C" bool __wrap_sg_isvalid(){return false;}extern "C" sg_stats __wrap_sg_query_stats(){return {};}
extern "C" void __wrap_sapp_cancel_quit(){}extern "C" int __wrap_sapp_get_num_dropped_files(){return 0;}extern "C" const char* __wrap_sapp_get_dropped_file_path(int){return "";}
int main(int argc,char** argv){
    getMainThreadId();Window peer;Native peerNative(peer);
    Window earlier; auto& main=earlier; earlierContext=&main.context(); Native mainNative(main);mainNative.win_id=702;internal::WindowRequestAccess::binding(main).handle={702};
    if(argc>1&&std::string(argv[1])=="--cross-buttons"){
        crossButtonMatrix(main,mainNative,true);crossButtonMatrix(peer,peerNative,true);
        std::printf("host-entry: %d checks, %d failures\n",checks,failures);return failures?1:0;
    }
    if(argc>1&&(std::string(argv[1])=="--touch-regressions"||std::string(argv[1])=="--base-behaviour")){
        const int filter=std::string(argv[1])=="--touch-regressions"?1:2;
        continuationMatrix(main,mainNative,filter);continuationMatrix(peer,peerNative,filter);
        std::printf("host-entry: %d checks, %d failures\n",checks,failures);return failures?1:0;
    }
    crossButtonMatrix(main,mainNative);crossButtonMatrix(peer,peerNative);
    reentrantVisibility(main,mainNative);reentrantVisibility(peer,peerNative);
    unheldGrabMotion(main,mainNative);unheldGrabMotion(peer,peerNative);
    continuationMatrix(main,mainNative);continuationMatrix(peer,peerNative);
    supersessionObservers(main,mainNative);supersessionObservers(peer,peerNative);
    std::vector<int> a,b;parity(main,mainNative,a);parity(peer,peerNative,b);check("every event type has main and secondary parity",a==b);
    clearInput(main);clearInput(peer);pointers(main,mainNative);pointers(peer,peerNative);
    nativeVisibilitySources(main,mainNative);nativeVisibilitySources(peer,peerNative);lockedDelivery(main,mainNative);lockedDelivery(peer,peerNative);visibility(main,mainNative,peer);visibility(peer,peerNative,main);
    clearInput(peer);injections(main,mainNative,peer);clickRecovery(main,mainNative);clickNewPress(main,mainNative);abortedClickOwnership(main,mainNative);disabledTouchInClick(main,mainNative);newestPressMatrix(main,mainNative);newestPressMatrix(peer,peerNative);touchExtremes(main,mainNative);touchExtremes(peer,peerNative);finiteExtremes(main,mainNative);finiteExtremes(peer,peerNative);nestedPointerBasis(main,mainNative);nestedPointerBasis(peer,peerNative);sourceParity(main,mainNative);sourceParity(peer,peerNative);touchIsolation(main,mainNative,peer,peerNative);closes(main,mainNative);closes(peer,peerNative);
    std::printf("host-entry: %d checks, %d failures\n",checks,failures);return failures?1:0;
}

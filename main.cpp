// ============================================================================================
// 我的第一个个人Raylib + Tiled RPG的C++项目
// 项目开始时间: 2025.12.22
// 该项目所有代码均为独自开发
// ============================================================================================

#define _CRT_SECURE_NO_WARNINGS

// --------------------------------------------------------------------------------------------
// 第三方库包含 (Relative Paths for Raylib in Packages)
// --------------------------------------------------------------------------------------------
#include "packages/raylib.5.5.0/build/native/include/raylib.h"
#include "packages/NolahanmaJson/json.hpp"
#include "packages/Tileson/tileson.hpp"

// --------------------------------------------------------------------------------------------
// 标准库包含
// --------------------------------------------------------------------------------------------
#include <iostream>
#include <vector>
#include <string>
#include <stdio.h>
#include <memory>
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <ctime>
#include <iomanip>
#include <queue>

using json = nlohmann::json;

// --------------------------------------------------------------------------------------------
// 宏定义
// --------------------------------------------------------------------------------------------
#define CAMERA_SCALE 3.5f

// ============================================================================================
// 游戏枚举与结构体定义
// ============================================================================================

// 游戏状态枚举
enum GameState {
    STATE_MENU,
    STATE_GAME,
};
// 玩家精灵方向枚举
enum PlayerDir {
    DIR_DOWN = 0, // 下
    DIR_UP = 1, // 上
    DIR_LEFT = 2, // 左
    DIR_RIGHT = 3 // 右
};
//玩家结构体
struct Player {
    Rectangle hitbox = { 100 + 4, 100 + 4, 24, 24 }; // 32-24=8，上下左右各留4像素缓冲
	Vector2 velocity = { 0.0f, 0.0f };//玩家x/y速度单位向量（方向）
	Texture2D sprites[4][4];// 4个方向，每个方向4帧动画
	float speed = 150.0f;// 玩家移动速度（像素/秒）
    PlayerDir currentDir = DIR_DOWN; // 默认方向（下）
    int currentFrame = 0; // 当前帧（0-3）
    int frameCounter = 0; // 帧计数器
    const int frameInterval = 8; // 动画速度（值越大越慢）
    int san = 100;//san值，后续影响速度
};


// A* 所需简单节点结构体
struct AStarNode {
    Vector2 pos;       // 世界坐标（瓦片中心）
    float g = 0.0f;    // 从起点到当前实际代价
    float h = 0.0f;    // 启发式到目标估计
    float f = 0.0f;    // g + h
    int parentIndex = -1;  // 父节点在 nodes vector 中的索引（-1 表示起点）

    bool operator>(const AStarNode& other) const { return f > other.f; } // 用于优先队列（小顶堆）
};

struct Monster {
    Rectangle hitbox = { 20, 20, 24, 24 };
    float speed = 150.0f * 0.96f;
    bool active = false;

    PlayerDir currentDir = DIR_DOWN;
    int currentFrame = 0;
    int frameCounter = 0;
    Texture2D sprites[4][4];
    const int frameInterval = 8;

    // --- 新增：A* 路径跟随系统 ---
    std::vector<Vector2> path;         // 计算出的路径点（世界坐标，中心点）
    int currentPathIndex = 0;           // 当前正在走向的路径索引
    float recalcTimer = 0.0f;          // 路径重算倒计时
    const float recalcInterval = 0.5f; // 每0.5秒强制重算一次（防止玩家突然转向）
    const float pathRecalcDistSq = 64.0f * 64.0f; // 玩家移动超过64px时重算
    Vector2 lastPlayerPosForPath;      // 上次计算路径时的玩家位置
};


// ==================== 瓦片集信息结构体（存储每个瓦片集的纹理+参数） ====================
struct TilesetData {
    tson::Tileset* tileset; // 瓦片集对象
    Texture2D texture; // 瓦片集纹理
    uint32_t firstGid; // 瓦片集起始GID
    int columns; // 瓦片集列数
};
//==============================对话框和对话贴图结构体==============================
// 大图布局配置（对应JSON的big_layout）
struct BigLayoutConfig {
    std::string type = ""; // single/dual/triple/quad
    int count = 0;
    std::vector<std::string> images;
};
// --------------------------
// 贴图总配置（对应JSON的avatar_config）
struct AvatarConfig {
    std::string small_avatar = "";
    BigLayoutConfig big_layout;
};
// --------------------------
//单个选项结构体（对应JSON的choices数组项）
struct Choice {
    std::string text;          // 选项文字
    std::string target_scene;  // 跨场景跳转目标
    std::string target_role;   // 场景内跳转目标
    std::string record_key;    // 对应的要记录的关键PlayerChoices的字段名（如"took_cursed_doll"）
    int record_value = -1;     // 要写入的选择值（-1为还没选）
};
// --------------------------
//  单个角色的对话结构体（对应JSON里的dialog_flow项）
struct RoleDialog {
    std::string role_id; // 角色ID（a/b/c/d）
    std::string role_name; // 角色显示名（村长A/村民B）
    std::vector<std::string> texts; // 角色的台词数组
    std::string next_role; // 下一个衔接的角色ID（空=最后一个）
    AvatarConfig avatar_config; // 贴图配置
    bool has_choices = false;  // 是否显示选项
    std::vector<Choice> choices; // 选项列表
    std::string branch_id = "main"; // 所属分支ID（初始默认main）
    std::string background = "";
};
// --------------------------
// 场景分支规则结构体（对应JSON的branch_rules数组项）
struct BranchRule {
    std::string trigger_key;   // 判定依据（PlayerChoices字段名）
    int trigger_value = -1;    // 判定值（-1为还没选，兜底为main分支）
    std::string target_branch_id; // 匹配后的分支ID
};
// --------------------------
//  单个场景的对话结构体（对应JSON里的scenes项）
struct SceneDialog {
    std::string scene_id; // 场景ID
    std::string trigger_type; // 触发类型（目前还只有auto，特定位置触发还没做）
    std::string trigger_condition; // 触发条件（enter_scene_）
    std::vector<RoleDialog> dialog_flow; // 场景内的角色对话流
    std::vector<BranchRule> branch_rules; // 分支判定规则
    std::string default_branch_id = "main"; // 默认分支ID
    std::vector<std::string> preset_plots;
    std::string plot_id;
};
// --------------------------
// 纯数据PlayerChoices结构体（关键选择记录）
struct PlayerChoices {
    int took_cursed_doll = -1;        // -1=未决定, 0=没拿, 1=拿了
    int cabinet_action = -1;        // -1=未互动, 0=没开, 1=用钥匙开了, 2=强行撬开
    int letter_disposal = -1;        // -1=未处理, 0=保留, 1=烧掉, 2=藏起来
    int trusted_villager = -1;        // -1=未表态, 0=不信, 1=相信
    int entered_basement = -1;        // -1=没进, 1=进去了
};
// --------------------------
// ==================== 传送点结构体 ====================
struct TeleportPoint {
    Rectangle area;               // 传送触发区域（像素坐标）
    std::string target_map_path;  // 目标地图完整路径（如Tile/secondMap.json）
    float target_x;               // 传送后玩家X（像素）
    float target_y;               // 传送后玩家Y（像素）
    
};
//=======================地图物品结构体================
struct ItemPoint {
    std::string map_id;
    std::string item_id;
    bool isPicked = false;
    int x;
    int y;
    std::string tip;
};
// --------------------------
//=======================物品栏单个物品的核心属性==========================
struct Item {
    std::string id;          // 物品唯一标识（如"rusty_key"，方便后续判定）
    std::string forDoor_id;  //对应的可以开启的门的id（要一模一样），没有则设为null
    std::string mapItemTip;  //地图物品提示，没有则设为null;
    std::string name;        // 显示名称（如"生锈的钥匙"）
    std::string desc;        // 物品说明（背包hover时显示，如"打开村长家柜子的钥匙"）
    int count = 0;           // 数量
    Texture2D iconTex;       // 加载后的图标纹理（避免重复加载）
    bool isExist;             //是否存在
    bool isInstructionExist;            // 是否存在说明
    Texture2D istTex;                   //纹理
    std::string ins;                    //说明
};

//=========================单个锁住家具属性=============================
struct LockedFuniture {
    std::string map_id;
    std::string funiture_id;
    bool isOpened = false;
    Rectangle area;
    std::string tip;
};

// ==================== 全局游戏数据结构体（封装所有全局状态） ====================
struct GameData {
    Player player;
    std::string current_map_id = "";

    // 已触发记录：改用plot_id（推荐用set加速查找）
    std::unordered_set<std::string> triggered_plots;
    // 可选辅助容器：通过plot_id快速查找（方便调试或扩展）
    std::unordered_map<std::string, SceneDialog> all_plots;
    // 主容器：按地图分组所有可能剧情段
    std::unordered_map<std::string, std::vector<SceneDialog>> plots_by_scene;
    std::string current_plot;

    std::vector<std::string> hasEnteredMaps;
    bool is_dialog_showing = false;
    SceneDialog current_scene_dialog;
    RoleDialog current_role_dialog;
    int current_line_index = 0;
    std::unordered_map<std::string, Texture2D> avatarTextureCache;
    float hintBlinkTimer = 0.0f;
    std::vector<TeleportPoint> teleportPoints; // 当前地图的传送点列表
    float last_trigger_time;      // 最后触发传送时间（冷却用）
    std::string current_map_path = "";         // 当前地图完整路径
    float game_time = 0.0f;                    // 游戏总时间（冷却判断）
    PlayerChoices player_choices;          // 关键选择记录
    std::vector<Choice> current_choices;   // 当前显示的选项列表（临时）
    int selected_choice_index = -1;        // 玩家选中的选项索引
    Item items[10];                        //player items
    int hoveredItemIndex = -1;             //物品栏鼠标索引
    double scaleX = 1.0;    // 宽度缩放因子（当前窗口宽/基准宽）
    double scaleY = 1.0;    // 高度缩放因子（当前窗口高/基准高）
    double scale = 0.71;     // 统一缩放因子（取min(scaleX, scaleY)，避免拉伸）
    bool isESC = false;     //是否在ESC界面
    std::vector<ItemPoint> itemPoints;//所有地图物品
    std::vector<LockedFuniture> funitures;//互动家具
    std::string save_date = ""; //存入存档的系统时间
    Monster monsterJ ;
    Monster monsterS;
    bool isInUi = false;
};

// ============================================================================================
// 存档与序列化定义 (JSON Macros)
// ============================================================================================
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Rectangle, x, y, width, height)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(PlayerChoices, took_cursed_doll)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Player, hitbox, san)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(ItemPoint, isPicked)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Item, id, count, isExist)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(LockedFuniture, isOpened)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Monster, active, hitbox)

// 核心存档结构
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(GameData,
    player,
    current_map_id,
    current_map_path,
    player_choices,
    items,
    save_date,
    monsterJ,
    triggered_plots,
    is_dialog_showing
)

// ============================================================================================
// 全局函数声明
// ============================================================================================

// --- 物理与地图相关 ---
bool IsTileCollidable(tson::Tile* tile);
bool CanMoveTo(Vector2 newPos, Rectangle hitbox, tson::Layer* collisionLayer, int tileW, int tileH);

// --- 对话系统 ---
bool LoadDialogsFromJSON(const std::string& jsonFilePath);
void CheckSceneDialogTrigger();
void InitDialogContext(const std::string& scene_id, const RoleDialog& targetRole);
void DrawDialogBox(const Font& chineseFont, const Camera2D& camera);
void UpdateDialogContext();
void ResetDialogContext();
Texture2D LoadAvatarTexture(const std::string& imgPath);
void UnloadAllAvatarTextures();

// --- 初始化与资源管理 ---
void InitializeWindowAndFont(Font& chineseFont, int width, int height);
bool LoadAllGameResources(
    tson::Tileson& parser,
    std::unique_ptr<tson::Map>& map,
    std::unordered_map<uint32_t, TilesetData>& tilesetMap,
    Player& player,
    tson::Layer*& groundLayer,
    tson::Layer*& collisionLayer,
    tson::Layer*& openedFunituresLayer,
    tson::Layer*& funitureLayer,
    int& tileWidth, int& tileHeight,
    int& mapWidth, int& mapHeight,
    Camera2D& camera,
    std::string mapPath);
bool Loadmaps(
    tson::Tileson& parser,
    std::unique_ptr<tson::Map>& map,
    std::unordered_map<uint32_t, TilesetData>& tilesetMap,
    tson::Layer*& groundLayer,
    tson::Layer*& collisionLayer,
    tson::Layer*& openedFunituresLayer,
    tson::Layer*& funitureLayer,
    int& tileWidth, int& tileHeight,
    int& mapWidth, int& mapHeight,
    Camera2D& camera,
    std::string mapPath);
bool LoadAllTextures(Player &player);
bool LoadOnceResource(tson::Tileson& parser, Player& player);
bool LoadItemsAndInteractions(tson::Tileson& parser);
void UnloadCurrentMapResources(std::unordered_map<uint32_t, TilesetData>& tilesetMap);
void UnloadAllResources(//游戏内重载
    Font& chineseFont,
    bool isGameResourcesLoaded,
    const std::unordered_map<uint32_t, TilesetData>& tilesetMap,
    const Player& player);

// --- 游戏状态处理 ---
void HandleGameInputAndToggleCollisionLayer(bool& showCollisionLayer);
void UpdatePlayerAndCameraAndChoiceAndMonster(
    Player& player,
    Monster& monster,
    tson::Layer* collisionLayer,
    int tileWidth, int tileHeight,
    Camera2D& camera);
void ConfirmSelectedChoice(int index);

// --- 渲染相关 ---
void RenderMenu( const Font& chineseFont);
void RenderGame(
    const Camera2D& camera,
    const tson::Map* map,
    int mapWidth, int mapHeight,
    int tileWidth, int tileHeight,
    tson::Layer* groundLayer,
    tson::Layer* collisionLayer,
    tson::Layer* openedFunituresLayer,
    tson::Layer* funitureLayer,
    const std::unordered_map<uint32_t, TilesetData>& tilesetMap,
    const Player& player,
    bool showCollisionLayer,
    const Font& chineseFont);
void RenderMap(
    const Camera2D& camera,
    const tson::Map* map,
    int mapWidth, int mapHeight,
    int tileWidth, int tileHeight,
    tson::Layer* groundLayer,
    tson::Layer* collisionLayer,
    tson::Layer* openedFunituresLayer,
    tson::Layer* funitureLayer,
    const std::unordered_map<uint32_t, TilesetData>& tilesetMap,
    bool showCollisionLayer);
void DrawTextExWithWrap(const Font& font, const std::string& text, Vector2 startPos, float fontSize, float spacing, float maxWidth, Color color);

// --- 传送点系统 ---
void CheckTeleportTrigger(
    Player& player,
    tson::Tileson& parser,
    std::unique_ptr<tson::Map>& map,
    std::unordered_map<uint32_t, TilesetData>& tilesetMap,
    tson::Layer*& groundLayer,
    tson::Layer*& collisionLayer,
    tson::Layer*& openedFunituresLayer,
    tson::Layer*& funitureLayer,
    int& tileWidth, int& tileHeight,
    int& mapWidth, int& mapHeight,
    Camera2D& camera);

// --- 物品与背包系统 ---
void InitMapItems();
void drawItemFlash();
void CheckPlayerWithMapItemsAndInfo(const Font& chineseFont, const Player& player);
void CheckIfLockedDoorCanBeOpened(const Font& chineseFont);
void DrawInventoryUI(const Font& chineseFont);

// --- 存档与菜单系统 ---
void InitTab();
void RenderTab(const Font& font);
void LoadGame(const Font& font);
void DoLoadGame(int index);
void GameElementsPassAndSive(int index);

// --- AI与寻路系统 ---
// ============================================================================================
// AI 与 A* 寻路系统实现
// ============================================================================================

std::vector<Vector2> FindPath(Vector2 startWorld, Vector2 goalWorld, Rectangle monsterHitbox, tson::Layer* collisionLayer, int tw, int th);
inline float DistSq(Vector2 a, Vector2 b);
void UpdateMonsterAI(Monster& monster, Player& player, tson::Layer* collisionLayer, int tw, int th);

// ============================================================================================
// 全局变量定义
// ============================================================================================
GameData g_gameData;
std::vector<std::string> allMaps;
std::vector<Item> allItems;
std::vector<std::string> savingTime(10, "null");
Item currentShowingItem;
GameState currentState = STATE_MENU;

// 音效资源
struct AllMusics
{
    Sound clickEscAndTab;
    Sound cancle;
    Sound save;
    Sound tip;
    Sound getThing;
};
AllMusics allMusics;

struct AllStickers//一些背景类的贴图
{
    Texture2D menuBg;
    Texture2D escBg;
    Texture2D tabBg;
    Texture2D itemFlash;

};
AllStickers allstickers;

// UI与状态标志
bool isWindow = true;
bool isSaving = false;
bool showSavingConfirm = false;
int selectedSavingIndex = -1;
bool isInTab = false;
float tabTime;
bool isLoading = false;
int selectedLoadingIndex = -1;
bool showLoadingConfirm = false;
bool hasConfirmLoad = false;
bool isItemShowing = false;

// ============================================================================================
// 主函数
// ============================================================================================
int main()
{
    const int SCREEN_WIDTH = 960*5/2;
    const int SCREEN_HEIGHT = 540*5/2;
    //所有地图
    allMaps.push_back("Eclassroom");
    allMaps.push_back("Eoffice");
    allMaps.push_back("EMtoilet");
    allMaps.push_back("EWtoilet");
    allMaps.push_back("Eliarbry");
    allMaps.push_back("Ecorridor");
    allMaps.push_back("EwaterRoom");
    allMaps.push_back("Ecanteen");

    // ==================== 窗口与字体初始化 ====================
    Font chineseFont;
    InitializeWindowAndFont(chineseFont,SCREEN_WIDTH , SCREEN_HEIGHT );
    SetExitKey(KEY_NULL);
    // ==================== 菜单he escBg资源初始化 ====================
    tabTime = GetTime();
    //****************************************************************一堆诡异的缩放设置
    int monitorWidth = GetMonitorWidth(0);  // 主显示器宽度
    int monitorHeight = GetMonitorHeight(0);// 主显示器高度
    g_gameData.scaleX = (float)monitorWidth / SCREEN_WIDTH * 2 / 3;
    g_gameData.scaleY = (float)monitorHeight / SCREEN_HEIGHT * 2 / 3;
    g_gameData.scale = (g_gameData.scaleX <= g_gameData.scaleY) ? g_gameData.scaleX : g_gameData.scaleY;
    TraceLog(LOG_INFO, "scale: %f", g_gameData.scale);
    TraceLog(LOG_INFO, "scaleX: %d", monitorWidth);
    TraceLog(LOG_INFO, "scaleY: %d", monitorHeight);
    SetWindowSize(SCREEN_WIDTH * g_gameData.scaleX, SCREEN_HEIGHT * g_gameData.scaleY);
    
    //*****************************************************************诡异的结束
    // ==================== 游戏状态与变量 ====================
    
    bool isGameResourcesLoaded = false;
    bool showCollisionLayer = true;

    tson::Tileson parser;
    std::unique_ptr<tson::Map> map;
    int tileWidth = 0, tileHeight = 0, mapWidth = 0, mapHeight = 0;
    std::unordered_map<uint32_t, TilesetData> tilesetMap;
    Player& player = g_gameData.player;
    Monster& monsterJ = g_gameData.monsterJ;
    Monster& monsterS = g_gameData.monsterS;
    tson::Layer* groundLayer = nullptr;
    tson::Layer* collisionLayer = nullptr;
    tson::Layer* openedFunituresLayer = nullptr;
    tson::Layer* funitureLayer = nullptr;
    Camera2D camera = { 0 };
    float cameraScale = CAMERA_SCALE;
    // -------------------- 主循环 --------------------
    while (!WindowShouldClose())
    {   //切换全屏与窗口缩放
        if (IsKeyPressed(KEY_F11))
        {
            if (isWindow) {//切到全屏
                g_gameData.scaleX = (double)monitorWidth / SCREEN_WIDTH ;
                g_gameData.scaleY = (double)monitorHeight / SCREEN_HEIGHT;
                g_gameData.scale = (g_gameData.scaleX <= g_gameData.scaleY) ? g_gameData.scaleX : g_gameData.scaleY;
                
                TraceLog(LOG_INFO, "scale: %f", g_gameData.scale);
                SetWindowSize(monitorWidth, monitorHeight);
                camera.zoom = cameraScale*g_gameData.scale;
                ToggleFullscreen();
                isWindow = false;
                TraceLog(LOG_INFO, "**切换到全屏，当前getscreenheight高度为：%d \n GetMointorHeight高度为%d\n scaleY为 %f", GetScreenHeight(), GetMonitorHeight(0),g_gameData.scaleY);
            }
            else {
                g_gameData.scaleX = (double)monitorWidth / SCREEN_WIDTH*2/3;
                g_gameData.scaleY = (double)monitorHeight / SCREEN_HEIGHT*2/3;
                g_gameData.scale = (g_gameData.scaleX <= g_gameData.scaleY) ? g_gameData.scaleX : g_gameData.scaleY;
                
                TraceLog(LOG_INFO, "scale: %f", g_gameData.scale);
                SetWindowSize(SCREEN_WIDTH * g_gameData.scaleX, SCREEN_HEIGHT * g_gameData.scaleY);
                camera.zoom = cameraScale * g_gameData.scale;
                ToggleFullscreen();
                isWindow = true;
                TraceLog(LOG_INFO, "**切换到窗口，当前getscreenheight高度为：%d \n GetMointorHeight高度为%d", GetScreenHeight(), GetMonitorHeight(0));
            }
        }

        if (currentState == STATE_MENU)
        {
            // ==================== 首次进入游戏时加载所有资源 ====================
            if (!isGameResourcesLoaded)
            {
                if (!LoadAllGameResources(parser, map, tilesetMap, player, groundLayer, collisionLayer, openedFunituresLayer, funitureLayer,
                    tileWidth, tileHeight, mapWidth, mapHeight, camera, "Tile\\Eclassroom.json"))// 初始地图路径
                {
                    break; // 加载失败已调用 CloseWindow()
                }
                if (!LoadOnceResource(parser, player)) {
                    TraceLog(LOG_ERROR, "游戏资源加载失败！检查资源路径和格式！");
                    CloseWindow;
                };
                isGameResourcesLoaded = true;
            }
            RenderMenu(chineseFont);
            
        }
        else if (currentState == STATE_GAME)
        {
            // ==================== 切换碰撞层显示/隐藏 ====================
            HandleGameInputAndToggleCollisionLayer(showCollisionLayer);
            //读档后重新加载全局
            if (hasConfirmLoad) {
                LoadAllGameResources(parser, map, tilesetMap, player, groundLayer, collisionLayer, openedFunituresLayer,funitureLayer,
                    tileWidth, tileHeight, mapWidth, mapHeight, camera, g_gameData.current_map_path);
                hasConfirmLoad = false;
            }
            

            // ==================== 玩家输入、移动、相机更新 ====================
            UpdatePlayerAndCameraAndChoiceAndMonster(player,monsterJ, collisionLayer, tileWidth, tileHeight, camera);

            // 调用传送检测函数
            CheckTeleportTrigger(player, parser, map, tilesetMap, groundLayer, collisionLayer, openedFunituresLayer,funitureLayer,
                tileWidth, tileHeight, mapWidth, mapHeight, camera);

            // ==================== 渲染游戏画面 ====================
            RenderGame(camera, map.get(), mapWidth, mapHeight, tileWidth, tileHeight,
                groundLayer, collisionLayer, openedFunituresLayer, funitureLayer,tilesetMap, player, showCollisionLayer, chineseFont);
            

           
        }
       

    }
    
    // ==================== 资源释放 ====================
    UnloadAllResources(chineseFont, isGameResourcesLoaded, tilesetMap, player);
    CloseWindow();
    return 0;
}

// ============================================================================================
// 初始化与窗口系统实现
// ============================================================================================

void InitializeWindowAndFont(Font& chineseFont, int weight, int height)
{
    InitWindow(weight, height, "myFirst Raylib + Tiled RPG");
    SetTargetFPS(60);

    // --- 中文字体加载 ---
    std::vector<int> chineseRange;
    for (int c = 0x20; c <= 0x7F; c++) chineseRange.push_back(c);
    for (int c = 0x4E00; c <= 0x9FFF; c++) chineseRange.push_back(c);
    for (int c = 0x3000; c <= 0x303F; c++) chineseRange.push_back(c);
    for (int c = 0xFF00; c <= 0xFFEF; c++) chineseRange.push_back(c);

    chineseFont = LoadFontEx(
        "font\\zh_font.ttf",
        48,
        chineseRange.data(),
        (int)chineseRange.size()
    );

    if (chineseFont.baseSize == 0) {
        TraceLog(LOG_ERROR, "中文字体加载失败！检查路径！");
        chineseFont = GetFontDefault();
    }
    else {
        TraceLog(LOG_INFO, "中文字体加载成功，加载字符数：%d", chineseFont.glyphCount);
    }
}

// ============================================================================================
// 地图与资源加载系统实现
// ============================================================================================

bool LoadAllGameResources(
    tson::Tileson& parser,
    std::unique_ptr<tson::Map>& map,
    std::unordered_map<uint32_t, TilesetData>& tilesetMap,
    Player& player,
    tson::Layer*& groundLayer,
    tson::Layer*& collisionLayer,
    tson::Layer*& openedFunituresLayer,
    tson::Layer*& funitureLayer,
    int& tileWidth, int& tileHeight,
    int& mapWidth, int& mapHeight,
    Camera2D& camera,
    std::string mapPath)
{
    // ==================== 加载指定地图 ====================
   if(!Loadmaps(parser, map, tilesetMap, groundLayer, collisionLayer, openedFunituresLayer,funitureLayer,
	   tileWidth, tileHeight, mapWidth, mapHeight, camera, mapPath))
	   return false;

    // 初始化相机
    camera.zoom = CAMERA_SCALE;
    float cameraScale = CAMERA_SCALE;
    camera.offset = { (float)GetScreenWidth() / 2, (float)GetScreenHeight() / 2 };
    camera.target = { player.hitbox.x + player.hitbox.width / 2, player.hitbox.y + player.hitbox.height / 2 };


    return true;
}

// ============================================================================================
// 游戏逻辑与输入处理实现
// ============================================================================================


void HandleGameInputAndToggleCollisionLayer(bool& showCollisionLayer)
{
    if (!isInTab && ((GetTime() - tabTime) > 0.5f)) {
        if (IsKeyPressed(KEY_TAB)) {
            TraceLog(LOG_WARNING, "*****切换到TAB");
            isInTab = true;
            PlaySound(allMusics.clickEscAndTab);
            tabTime = GetTime();
            isItemShowing = false;
        }
    }
    
    // --- 切换碰撞层显示/隐藏 ---
    if (IsKeyPressed(KEY_F1)) {
        showCollisionLayer = !showCollisionLayer;
        TraceLog(LOG_INFO, "碰撞层显示状态：%s", showCollisionLayer ? "显示" : "隐藏");
    }
    
    // --- ESC 菜单处理 ---
    if (!isSaving && !isLoading && !isItemShowing) {
        if (IsKeyPressed(KEY_ESCAPE)) {
            g_gameData.isESC = !g_gameData.isESC;
            if (g_gameData.isESC) PlaySound(allMusics.clickEscAndTab);
            else PlaySound(allMusics.cancle);
        }
    }

    if (g_gameData.isESC) {
        if (IsKeyPressed(KEY_Y)) {
            CloseWindow();
            exit(0);
        }
        if (IsKeyPressed(KEY_N)) g_gameData.isESC = false;
    }
    //判定是否在ui界面，在则停止玩家和怪物移动
    if(isInTab || g_gameData.isESC || isItemShowing || isLoading) {
		g_gameData.isInUi = true;
	}
	else { g_gameData.isInUi = false; }
}

void UpdatePlayerAndCameraAndChoiceAndMonster(
    Player& player,
    Monster& monster,
    tson::Layer* collisionLayer,
    int tileWidth, int tileHeight,
    Camera2D& camera)
{
    camera.offset = { (float)GetScreenWidth() / 2, (float)GetScreenHeight() / 2 };
    
    // --- 对话模式下的逻辑 ---
    if (g_gameData.is_dialog_showing) {
        // --- 选项输入处理 ---
        if (!g_gameData.current_choices.empty()) {
            if (g_gameData.selected_choice_index == -1) {
                g_gameData.selected_choice_index = 0;
            }
            if (IsKeyPressed(KEY_UP) && g_gameData.selected_choice_index > 0) {
                g_gameData.selected_choice_index--;
            }
            if (IsKeyPressed(KEY_DOWN) && g_gameData.selected_choice_index < (int)g_gameData.current_choices.size() - 1) {
                g_gameData.selected_choice_index++;
            }
            if (IsKeyPressed(KEY_R) && !isInTab && !g_gameData.isESC) {
                if (g_gameData.selected_choice_index >= 0 && g_gameData.selected_choice_index < (int)g_gameData.current_choices.size()) {
                    ConfirmSelectedChoice(g_gameData.selected_choice_index);
                }
                g_gameData.selected_choice_index = -1;
            }
        }
        
        // --- 对话翻页处理 ---
        if (!g_gameData.isESC && !isInTab && (IsKeyPressed(KEY_F) || IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) && g_gameData.current_choices.empty()) {
            UpdateDialogContext();
        }
        
        player.velocity = { 0, 0 }; // 对话时停止移动
        camera.target = { player.hitbox.x + player.hitbox.width / 2, player.hitbox.y + player.hitbox.height / 2 };
        return;
    }

    // --- 玩家移动逻辑 ---
    Vector2 input = { 0, 0 };
    bool isMoving = false;
    if (IsKeyDown(KEY_W)) { input.y -= 1; player.currentDir = DIR_UP; isMoving = true; }
    if (IsKeyDown(KEY_S)) { input.y += 1; player.currentDir = DIR_DOWN; isMoving = true; }
    if (IsKeyDown(KEY_A)) { input.x -= 1; player.currentDir = DIR_LEFT; isMoving = true; }
    if (IsKeyDown(KEY_D)) { input.x += 1; player.currentDir = DIR_RIGHT; isMoving = true; }
    
    if (input.x != 0 && input.y != 0) {
        input.x *= 0.707f;
        input.y *= 0.707f;
    }
    
    if (player.velocity.x != 0 || player.velocity.y != 0) isMoving = true;
    
    player.velocity.x = input.x * player.speed;
    player.velocity.y = input.y * player.speed;
    
    // --- 动画帧更新 ---
    if (isMoving) {
        player.frameCounter++;
        if (player.frameCounter >= player.frameInterval) {
            player.currentFrame = (player.currentFrame + 1) % 4;
            player.frameCounter = 0;
        }
    } else {
        player.frameCounter = 0;
    }
    
    if (isInTab || g_gameData.isESC || isItemShowing) player.velocity = { 0, 0 };
    
    Vector2 newPos = {
        player.hitbox.x + player.velocity.x * GetFrameTime(),
        player.hitbox.y + player.velocity.y * GetFrameTime()
    };
    
    if (CanMoveTo({ newPos.x, player.hitbox.y }, player.hitbox, collisionLayer, tileWidth, tileHeight)) {
        player.hitbox.x = newPos.x;
    }
    if (CanMoveTo({ player.hitbox.x, newPos.y }, player.hitbox, collisionLayer, tileWidth, tileHeight)) {
        player.hitbox.y = newPos.y;
    }
    
    camera.target = {
        player.hitbox.x + player.hitbox.width / 2,
        player.hitbox.y + player.hitbox.height / 2
    };

    // --- 怪物更新 ---
    if (IsKeyPressed(KEY_Q)) {
        g_gameData.monsterJ.active = !g_gameData.monsterJ.active;
    }
    UpdateMonsterAI(g_gameData.monsterJ, player, collisionLayer, tileWidth, tileHeight);
}

// ============================================================================================
// 渲染系统实现
// ============================================================================================

void RenderMenu( const Font& chineseFont)
{
    if (isLoading) {
        BeginDrawing();
        // 1. 获取屏幕尺寸（基础参考）
        int screenWidth = GetScreenWidth();            // 游戏窗口屏幕宽度（像素）
        int screenHeight = GetScreenHeight();          // 游戏窗口屏幕高度（像素）
        if (!isWindow)screenHeight = GetMonitorHeight(0);

        Rectangle edst = { 0,0,screenWidth,screenHeight };
        if (allstickers.tabBg.id != 0) {
            //TraceLog(LOG_INFO, "***绘制TAB背景ing");
            DrawTexturePro(allstickers.tabBg,
                { 0, 0, (float)allstickers.tabBg.width, (float)allstickers.tabBg.height },
                { 0, 0, float(screenWidth), float(screenHeight) },
                { 0, 0 }, 0.0f, WHITE);

        }
        else {
            TraceLog(LOG_WARNING, "tabBg没有成功载入");
        }
        LoadGame(chineseFont);
        //TraceLog(LOG_INFO, "渲染Load");
        EndDrawing();
    }
    else {
        int SCREEN_WIDTH = GetScreenWidth();
        int SCREEN_HEIGHT = GetScreenHeight();
        Rectangle startButton = { (SCREEN_WIDTH / 2.0f - 120.0f * (float)g_gameData.scale), (SCREEN_HEIGHT / 2.0f - 45 * g_gameData.scale) , 240.0f * (float)g_gameData.scale, 90.0f * (float)g_gameData.scale };
        BeginDrawing();
        ClearBackground(BLACK);

        if (allstickers.menuBg.id != 0) {
            DrawTexturePro(allstickers.menuBg,
                { 0, 0, (float)allstickers.menuBg.width, (float)allstickers.menuBg.height },
                { 0, 0, (float)SCREEN_WIDTH, (float)SCREEN_HEIGHT },
                { 0, 0 }, 0.0f, WHITE);
        }
        /*
        const char* gameTitle = u8"宿中的另一面";
        Vector2 titleSize = MeasureTextEx(chineseFont, gameTitle, 72 * g_gameData.scale, 0);
        Vector2 titlePos = {
            SCREEN_WIDTH / 2 - titleSize.x / 2,
            SCREEN_HEIGHT / 2 - 80
        };
        DrawTextEx(chineseFont, gameTitle, titlePos, 72 * g_gameData.scale, 0, RED);
        */
        const char* startText = u8"开始追忆";
        int fontSize = 48 * g_gameData.scale;
        Vector2 buttonTextSize = MeasureTextEx(chineseFont, startText, fontSize, 0);
        Vector2 buttonTextPos = {
            startButton.x + (startButton.width - buttonTextSize.x) / 2,
            startButton.y + 22 * (float)g_gameData.scale
        };
        Vector2 mousePos = GetMousePosition();

        if (CheckCollisionPointRec(mousePos, startButton)) {
            DrawRectangleRec(startButton, Fade(PURPLE, 0.8f));
            DrawTextEx(chineseFont, startText, buttonTextPos, fontSize, 0, RED);
        }
        else {
            DrawRectangleRec(startButton, Fade(ORANGE, 0.8f));
            DrawTextEx(chineseFont, startText, buttonTextPos, fontSize, 0, BLACK);
        }
        DrawRectangleLinesEx(startButton, 4 * g_gameData.scale, BLACK);


        //绘制读档
        float saveWidth = 240 * g_gameData.scale;
        float saveHeight = 90 * g_gameData.scale;
        Rectangle save{ GetScreenWidth() / 2 - saveWidth / 2,GetScreenHeight() / 4 ,saveWidth,saveHeight };
        const char* Ltext = u8"读取存档";
        int textWidth = MeasureText(Ltext, fontSize);
        Rectangle load{ startButton.x,startButton.y + 2 * startButton.height,startButton.width,startButton.height };
        Vector2 loadTextSize = MeasureTextEx(chineseFont, Ltext, fontSize, 0);
        Vector2 loadPos = {
           load.x + (load.width - loadTextSize.x) / 2,
           load.y + 22 * (float)g_gameData.scale
        };
        if (CheckCollisionPointRec(mousePos, load)) {
            DrawRectangleRec(load, Fade(PURPLE, 0.8f));
            DrawTextEx(chineseFont, Ltext, loadPos, fontSize, 0, GREEN);
        }
        else {
            DrawRectangleRec(load, Fade(ORANGE, 0.8f));
            DrawTextEx(chineseFont, Ltext, loadPos, fontSize, 0, BLACK);
        }
        DrawRectangleLinesEx(load, 4 * g_gameData.scale, BLACK);

        EndDrawing();
        //执行对应操作
        if (CheckCollisionPointRec(mousePos, startButton) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !isLoading) {
            currentState = STATE_GAME;
        }
        if (CheckCollisionPointRec(mousePos, load) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            isLoading = true;
            TraceLog(LOG_INFO, "isLoading==true");
        }
    }

}

void RenderGame(
    const Camera2D& camera,
    const tson::Map* map,
    int mapWidth, int mapHeight,
    int tileWidth, int tileHeight,
    tson::Layer* groundLayer,
    tson::Layer* collisionLayer,
    tson::Layer* openedFunituresLayer,
    tson::Layer* funitureLayer,
    const std::unordered_map<uint32_t, TilesetData>& tilesetMap,
    const Player& player,
    bool showCollisionLayer,
    const Font& chineseFont)
{
    BeginDrawing();
    ClearBackground(BLACK);

    BeginMode2D(camera);

	// 绘制地图层
    RenderMap(camera, map, mapWidth, mapHeight, tileWidth, tileHeight,
		groundLayer, collisionLayer, openedFunituresLayer, funitureLayer, tilesetMap, showCollisionLayer);

    //绘制物品闪烁flash
    drawItemFlash();

    //绘制怪物\调试
    if (g_gameData.monsterJ.active) {
        Texture2D currentSpriteM = g_gameData.monsterJ.sprites[g_gameData.monsterJ.currentDir][g_gameData.monsterJ.currentFrame];

        DrawTexturePro(
            currentSpriteM,
            { 0, 0, (float)currentSpriteM.width, (float)currentSpriteM.height },
            { g_gameData.monsterJ.hitbox.x - (currentSpriteM.width * (float)g_gameData.scale * 4 / 10 - g_gameData.monsterJ.hitbox.width) / 2,
            g_gameData.monsterJ.hitbox.y - (currentSpriteM.height * (float)g_gameData.scale * 7 / 10 - g_gameData.monsterJ.hitbox.height) / 2,
            (float)currentSpriteM.width * (float)g_gameData.scale * 4 / 10,
            (float)currentSpriteM.height * (float)g_gameData.scale * 4 / 10 },
            { 0, 0 },
            0.0f,
            WHITE
        );

    }

    // 绘制玩家
    // 获取当前纹理
    
    Texture2D currentSprite = player.sprites[player.currentDir][player.currentFrame];
    // 容错：加载失败兜底显示down0
    if (currentSprite.id == 0) {
        currentSprite = player.sprites[DIR_DOWN][0];
    }

    DrawTexturePro(
        currentSprite,
        { 0, 0, (float)currentSprite.width, (float)currentSprite.height },
        { player.hitbox.x - (currentSprite.width * 1 / 4 * (float)g_gameData.scale - player.hitbox.width) / 2,
        player.hitbox.y - (currentSprite.height * 4 / 10 * (float)g_gameData.scale - player.hitbox.height) / 2,
        (float)currentSprite.width* (float)g_gameData.scale*1/4,
        (float)currentSprite.height* (float)g_gameData.scale*1/4},
        { 0, 0 },
        0.0f,
        WHITE
    );


    // ===================== 调用对话框绘制函数 =====================
    if (g_gameData.is_dialog_showing) { // 仅对话框激活时绘制
        DrawDialogBox(chineseFont, camera);
    }

    EndMode2D();//******************************************************************
    //******************************************************************************
    //******************************************************************************

    // 绘制UI===============================================================

    //绘制物品捡拾提示
    if(!isInTab&&!g_gameData.isESC)CheckPlayerWithMapItemsAndInfo(chineseFont, player);
    //锁住的门提示
    if (!isInTab&&!g_gameData.isESC)CheckIfLockedDoorCanBeOpened(chineseFont);
    //顶部ui
    const char* uiText = u8"WASD 移动  Raylib + Tiled JSON";
    Vector2 uiPos = { 10, 10 };
    DrawTextEx(chineseFont, uiText, uiPos, 24, 0, LIME);
    DrawFPS(10, 40);
    // 需要先用 std::string 临时变量保存拼接结果，再调用 c_str()，保证生命周期
    std::string sceneTextStr = u8"房间  " + g_gameData.current_map_id;
    const char* sceneText = sceneTextStr.c_str();
    DrawTextEx(chineseFont, sceneText, { 10, 60 }, 24, 0, SKYBLUE);
    //绘制物品
    if (!g_gameData.isESC && !g_gameData.is_dialog_showing && !isSaving && !isLoading) {
        DrawInventoryUI(chineseFont);
    }
    //TABUI
    //绘制tab背景=====================================================
    if (isInTab) {
        // 1. 获取屏幕尺寸（基础参考）
        int screenWidth = GetScreenWidth();            // 游戏窗口屏幕宽度（像素）
        int screenHeight = GetScreenHeight();          // 游戏窗口屏幕高度（像素）
        if (!isWindow)screenHeight = GetMonitorHeight(0);

        Rectangle edst = { 0,0,screenWidth,screenHeight };
        if (allstickers.tabBg.id != 0) {
            //TraceLog(LOG_INFO, "***绘制TAB背景ing");
            DrawTexturePro(allstickers.tabBg,
                { 0, 0, (float)allstickers.tabBg.width, (float)allstickers.tabBg.height },
                { 0, 0, float(screenWidth), float(screenHeight) },
                { 0, 0 }, 0.0f, WHITE);

        }
        else {
            TraceLog(LOG_WARNING, "tabBg没有成功载入");
        }


        Rectangle sprite{ 0 + screenWidth / 10,
            0 + screenHeight / 4,
            g_gameData.player.sprites[0][0].width * 2 *g_gameData.scale,
            g_gameData.player.sprites[0][0].height * 2 *g_gameData.scale
        };

        DrawTexturePro(g_gameData.player.sprites[0][0],
            { 0, 0, (float)g_gameData.player.sprites[0][0].width, (float)g_gameData.player.sprites[0][0].height },
            sprite,
            { 0, 0 }, 0.0f, WHITE);

    }
    if(!g_gameData.isESC)
    if (isInTab) {
        RenderTab(chineseFont);
    }

    //draw ESC ui
        //=========================draw EXIT_BG===========================
    if (g_gameData.isESC) {
        // ===================== 相机视口坐标计算（核心！适配大地图+相机缩放） =====================
        // 1. 获取屏幕尺寸（基础参考）
        const int screenWidth = GetScreenWidth();            // 游戏窗口屏幕宽度（像素）
        const int screenHeight = GetScreenHeight();          // 游戏窗口屏幕高度（像素）

        Rectangle edst = { screenWidth / 4 ,0,screenWidth / 2,screenHeight };
        DrawTexturePro(
            allstickers.escBg,
            { 0, 0, (float)allstickers.escBg.width, (float)allstickers.escBg.height },
            edst,
            { 0, 0 },
            0.0f,
            WHITE
        );
        DrawRectangle(0, 0, screenWidth, screenHeight, Fade(BLACK, 0.6f));
        int Ewidth = 1000 * g_gameData.scale;
        int Eheight = 500 * g_gameData.scale;

        Rectangle escui{ screenWidth / 2 - Ewidth / 2,screenHeight / 2 - Eheight * 2 / 3,Ewidth,Eheight };
        DrawRectangleRec(escui, Fade(GRAY, 0.5f));
        DrawRectangleLinesEx(escui, 4, YELLOW);
        const char* esctip = u8" 你要不玩了吗? ";
        const char* escconfirm = u8" 按Y退出  N取消";
        int fontSize = 98 * g_gameData.scale;
        DrawTextEx(chineseFont, esctip, { escui.x + fontSize * 3 / 2 ,escui.y + fontSize }, fontSize, 0, YELLOW);
        DrawTextEx(chineseFont, escconfirm, { escui.x + fontSize ,escui.y + escui.height / 2 + fontSize / 2 }, fontSize, 0, RED);
        
    }
    EndDrawing();
}

void UnloadAllResources(
    Font& chineseFont,
    bool isGameResourcesLoaded,
    const std::unordered_map<uint32_t, TilesetData>& tilesetMap,
    const Player& player)
{
    // --- 释放所有纹理与字体资源 ---
    if (allstickers.menuBg.id != 0) UnloadTexture(allstickers.menuBg);
    if (allstickers.tabBg.id != 0) UnloadTexture(allstickers.tabBg);
    if (allstickers.escBg.id != 0) UnloadTexture(allstickers.escBg);
    if (allstickers.itemFlash.id != 0) UnloadTexture(allstickers.itemFlash);
    if (chineseFont.baseSize != 0 && chineseFont.texture.id != GetFontDefault().texture.id) {
        UnloadFont(chineseFont);
    }
    
    if (isGameResourcesLoaded) {
        // 遍历释放所有瓦片集的纹理
        for (auto& pair : tilesetMap) {
            UnloadTexture(pair.second.texture);
        }
        // 遍历释放16张玩家动画帧
        for (int dir = 0; dir < 4; dir++) {
            for (int frame = 0; frame < 4; frame++) {
                UnloadTexture(player.sprites[dir][frame]);
            }
        }

        for (int dir = 0; dir < 4; dir++) {
            for (int frame = 0; frame < 4; frame++) {
                UnloadTexture(g_gameData.monsterJ.sprites[dir][frame]);
            }
        }

        UnloadAllAvatarTextures();

        //卸载音乐资源
        UnloadSound(allMusics.clickEscAndTab);
        UnloadSound(allMusics.cancle);
        UnloadSound(allMusics.save);
        UnloadSound(allMusics.getThing);
        UnloadSound(allMusics.tip);
        UnloadTexture(allstickers.tabBg);
        UnloadTexture(allstickers.escBg);


    }
   


    
}

// ============================================================================================
// 碰撞检测与物理系统实现
// ============================================================================================

bool CanMoveTo(Vector2 newPos, Rectangle hitbox, tson::Layer* collisionLayer, int tileW, int tileH) {
    Rectangle newHitbox{ newPos.x,newPos.y,20,20 };//减了4更美观
    // 空指针检测，避免崩溃
    if (!collisionLayer || !collisionLayer->getMap()) {
        TraceLog(LOG_ERROR, "collisionLayer为空或关联的map为空！");
        return false;
    }
    // 边界检测：缩小碰撞盒后，边界计算要留缓冲
    int mapTotalWidth = tileW * collisionLayer->getMap()->getSize().x;
    int mapTotalHeight = tileH * collisionLayer->getMap()->getSize().y;
    // 碰撞盒边缘留2像素缓冲，避免贴边卡墙
    if (newPos.x < 2 || newPos.y < 2 ||
        newPos.x + hitbox.width > mapTotalWidth - 2 ||
        newPos.y + hitbox.height > mapTotalHeight - 2) {
        return false;
    }
    // 采样逻辑：取「碰撞盒中心」+ 4个方向缓冲点
    // 核心：采样点远离瓦片边缘，避免精准卡墙
    int centerX = (int)(newPos.x + hitbox.width / 2);
    int centerY = (int)(newPos.y + hitbox.height / 2);
    // 采样点：中心 + 上下左右各偏移8像素（避开边缘）
    tson::Tile* tiles[5] = {
        collisionLayer->getTileData(centerX / tileW, centerY / tileH), // 中心
        collisionLayer->getTileData((centerX + 8) / tileW, centerY / tileH), // 右
        collisionLayer->getTileData((centerX - 8) / tileW, centerY / tileH), // 左
        collisionLayer->getTileData(centerX / tileW, (centerY + 8) / tileH), // 下
        collisionLayer->getTileData(centerX / tileW, (centerY - 8) / tileH) // 上
    };
    // 检测采样点是否碰撞
    for (int i = 0; i < 5; i++) {
        if (IsTileCollidable(tiles[i])) return false;
    }
    //锁住的门碰撞
    for (auto funiture : g_gameData.funitures) {
        if (funiture.map_id == g_gameData.current_map_id && !funiture.isOpened) {
            if(CheckCollisionRecs(funiture.area, newHitbox) ){
                
                return false;
            }
        }
    }
    return true;
}

bool IsTileCollidable(tson::Tile* tile) {
    // 空瓦片 → 不碰撞
    if (!tile) return false;
    // 提取纯GID（剥离翻转/旋转位）
    uint32_t gid = tile->getGid();
    uint32_t pureGid = gid & ~(0x80000000 | 0x40000000 | 0x20000000);
    // 非空瓦片（pureGid≠0）→ 碰撞
    return pureGid != 0;
}

// ============================================================================================
// 对话系统与 JSON 解析实现
// ============================================================================================

bool LoadDialogsFromJSON(const std::string& jsonFilePath) {
    //  打开JSON文件
    std::ifstream jsonFile(jsonFilePath,std::ios::in | std::ios::binary);
    if (!jsonFile.is_open()) {
        TraceLog(LOG_ERROR, "对话文件打开失败：%s", jsonFilePath.c_str());
        return false;
    }
    else { TraceLog(LOG_INFO, "对话json打开成功"); }

    //  解析JSON文件到json对象
    json dialogJson;
    try {
        jsonFile >> dialogJson; // 把文件内容读入JSON对象
    }
    catch (const json::parse_error& e) {
        TraceLog(LOG_ERROR, "JSON解析失败：%s，错误位置：%d", e.what(), e.byte);
        jsonFile.close();
        return false;
    }
    //  遍历JSON里的scenes数组，解析到SceneDialog结构体
    for (const auto& sceneItem : dialogJson["scenes"]) {
        SceneDialog sceneDialog;
        // 解析场景基础字段
        sceneDialog.scene_id = sceneItem["scene_id"];
        // plot_id 必须存在
        if (sceneItem.contains("plot_id")) {
            sceneDialog.plot_id = sceneItem["plot_id"].get<std::string>();
            //TraceLog(LOG_INFO, "plot_id写入成功:%s", sceneDialog.plot_id.c_str());
        }

        //sceneDialog.trigger_type = sceneItem["trigger_type"];
        //sceneDialog.trigger_condition = sceneItem["trigger_condition"];
        //sceneDialog.auto_close = sceneItem["auto_close"];

        if (sceneItem.contains("preset_plots") && sceneItem["preset_plots"].is_array()) {
            for (const auto& pre : sceneItem["preset_plots"]) {
                sceneDialog.preset_plots.push_back(pre.get<std::string>());

            }
        }
        //else { TraceLog(LOG_INFO, ("plot:%s不含前置剧情"), sceneDialog.plot_id.c_str()); }


        // 新增：解析场景分支规则（branch_rules）
        if (sceneItem.contains("branch_rules") && sceneItem["branch_rules"].is_array()) {
            for (const auto& ruleItem : sceneItem["branch_rules"]) {
                BranchRule rule;
                rule.trigger_key = ruleItem["trigger_key"];
                rule.trigger_value = ruleItem["trigger_value"];
                rule.target_branch_id = ruleItem["target_branch_id"];
                sceneDialog.branch_rules.push_back(rule);
            }
        }//else{ TraceLog(LOG_INFO, ("plot:%s不含分支"), sceneDialog.plot_id.c_str()); }

        // 解析默认分支ID
        sceneDialog.default_branch_id = sceneItem.value("default_branch_id", "main");
        //TraceLog(LOG_INFO, ("plot:%s的分配分支ID为%s"), sceneDialog.plot_id.c_str(), sceneDialog.default_branch_id.c_str());
        
        //  解析场景内的dialog_flow（角色对话流）
        for (const auto& roleItem : sceneItem["dialog_flow"]) {
            RoleDialog roleDialog;
           // TraceLog(LOG_INFO, "解析dialog_flow");
            roleDialog.role_id = roleItem["role_id"];
            //TraceLog(LOG_INFO, "role_id:%s", roleDialog.role_id.c_str());
            roleDialog.role_name = roleItem["role_name"];
            //TraceLog(LOG_INFO, "role_name:%s", roleDialog.role_name.c_str());
            roleDialog.next_role = roleItem["next_role"];
            //TraceLog(LOG_INFO, "next_role:%s", roleDialog.next_role.c_str());
            // 解析角色的texts数组（台词）
            for (const auto& text : roleItem["text"]) {
                roleDialog.texts.push_back(text);
                //TraceLog(LOG_INFO, "写入台词组");
            }
            // 解析贴图：解析avatar_config贴图配置
            if (roleItem.contains("avatar_config")) {
                const auto& avatarConfigJson = roleItem["avatar_config"];
                // 解析小图路径
                if (avatarConfigJson.contains("small_avatar")) {
                    roleDialog.avatar_config.small_avatar = avatarConfigJson["small_avatar"];
                }
                // 解析大图布局
                if (avatarConfigJson.contains("big_layout")) {
                    const auto& bigLayoutJson = avatarConfigJson["big_layout"];
                    if (bigLayoutJson.contains("type")) {
                        roleDialog.avatar_config.big_layout.type = bigLayoutJson["type"];
                    }
                    if (bigLayoutJson.contains("count")) {
                        roleDialog.avatar_config.big_layout.count = bigLayoutJson["count"];
                    }
                    // 解析大图列表
                    if (bigLayoutJson.contains("images") && bigLayoutJson["images"].is_array()) {
                        for (const auto& imgPath : bigLayoutJson["images"]) {
                            roleDialog.avatar_config.big_layout.images.push_back(imgPath);
                        }
                    }
                    // 容错：count不能超过images数量
                    if (roleDialog.avatar_config.big_layout.count > roleDialog.avatar_config.big_layout.images.size()) {
                        roleDialog.avatar_config.big_layout.count = roleDialog.avatar_config.big_layout.images.size();
                    }
                }
                
            }
            //解析背景
            if (roleItem.contains("background")) {
                roleDialog.background = roleItem["background"];
                //TraceLog(LOG_INFO, "写入背景");
            }
            

            // 新增：解析has_choices
            roleDialog.has_choices = roleItem.value("has_choices", false);
            // 解析choices数组
            if (roleItem.contains("choices") && roleItem["choices"].is_array()) {
                for (const auto& choiceItem : roleItem["choices"]) {
                    Choice choice;
                    choice.text = choiceItem["text"];
                    choice.target_scene = choiceItem.value("target_scene", "");
                    choice.target_role = choiceItem.value("target_role", "");
                    choice.record_key = choiceItem.value("record_key", "");
                    choice.record_value = choiceItem.value("record_value", -1);
                    roleDialog.choices.push_back(choice);
                }
            }
            // 解析branch_id
            roleDialog.branch_id = roleItem.value("branch_id", "main");

            // 把角色对话加入场景的dialog_flow
            sceneDialog.dialog_flow.push_back(roleDialog);
        }
        // 存入容器
        g_gameData.plots_by_scene[sceneDialog.scene_id].push_back(sceneDialog);
        g_gameData.all_plots[sceneDialog.plot_id] = sceneDialog;

        TraceLog(LOG_INFO, "成功加载场景对话：%s，包含%d个角色",
            sceneDialog.scene_id.c_str(), sceneDialog.dialog_flow.size());
    }
    // 关闭文件，返回成功
    jsonFile.close();
    TraceLog(LOG_INFO, "所有对话文件加载完成，共加载%d个场景", g_gameData.plots_by_scene.size());
    return true;
}

void CheckSceneDialogTrigger() {

    std::string currentSceneId = g_gameData.current_map_id;
    if (currentSceneId == "UNKNOWN") return;
    auto it = g_gameData.plots_by_scene.find(currentSceneId);
    if (it == g_gameData.plots_by_scene.end()) return;
    const auto& candidates = it->second;  // 当前地图的所有剧情段

    for (const auto& plot : candidates) {
        // 1. 是否已触发（用plot_id检查）
        bool already_triggered = g_gameData.triggered_plots.count(plot.plot_id) > 0;

        // 2. 前置条件：所有preset_plots都必须已触发
        bool preset_ok = true;
        for (const auto& pre : plot.preset_plots) {
            if (g_gameData.triggered_plots.count(pre) == 0) {
                preset_ok = false;
                break;
            }
        }

        //  触发条件：当前剧情未触发 + 无对话框显示 + 剧情对话存在 + 触发过前置场景
        if (!already_triggered && !g_gameData.is_dialog_showing && preset_ok) {
            std::string plot_id = plot.plot_id;
            SceneDialog currentScene = plot;
            TraceLog(LOG_INFO, "===== 触发场景对话：%s =====", plot_id.c_str());
            //TraceLog(LOG_INFO, "场景触发条件：%s", currentScene.trigger_condition.c_str());
            // ==================== 分支判定逻辑 ====================
            std::string targetBranchId = currentScene.default_branch_id; // 默认分支ID
            // 1. 若场景有分支规则，匹配玩家选择
            if (!currentScene.branch_rules.empty()) {
                for (const auto& rule : currentScene.branch_rules) {
                    int currentValue = -1;
                    // 匹配trigger_key到PlayerChoices的字段值
                    if (rule.trigger_key == "took_cursed_doll") {
                        currentValue = g_gameData.player_choices.took_cursed_doll;
                    }
                    else if (rule.trigger_key == "cabinet_action") {
                        currentValue = g_gameData.player_choices.cabinet_action;
                    }
                    else if (rule.trigger_key == "letter_disposal") {
                        currentValue = g_gameData.player_choices.letter_disposal;
                    }
                    else if (rule.trigger_key == "trusted_villager") {
                        currentValue = g_gameData.player_choices.trusted_villager;
                    }
                    else if (rule.trigger_key == "entered_basement") {
                        currentValue = g_gameData.player_choices.entered_basement;
                    }

                    // 匹配成功则更新目标分支ID，跳出循环
                    if (currentValue == rule.trigger_value) {
                        targetBranchId = rule.target_branch_id;
                        TraceLog(LOG_INFO, "匹配分支规则：%s=%d → 分支ID：%s",
                            rule.trigger_key.c_str(), rule.trigger_value, targetBranchId.c_str());
                        break;
                    }
                }
            }
            // 2. 找到目标分支的第一个角色
            RoleDialog targetRole;
            bool foundBranch = false;
            for (const auto& role : currentScene.dialog_flow) {
                if (role.branch_id == targetBranchId) {
                    targetRole = role;
                    foundBranch = true;
                    break;
                }
            }
            // 容错：未找到分支则取第一个角色
            if (!foundBranch) {
                targetRole = currentScene.dialog_flow[0];
                TraceLog(LOG_WARNING, "未找到分支%s，默认取第一个角色", targetBranchId.c_str());
            }

            if (!currentScene.dialog_flow.empty()) {
                //打印分支对应的角色和台词
                TraceLog(LOG_INFO, "第一个说话的角色：%s", targetRole.role_name.c_str());
                TraceLog(LOG_INFO, "第一句台词：%s", targetRole.texts[0].c_str());
                // 初始化对话上下文时传入目标分支角色
                InitDialogContext(plot_id, targetRole);

            }
            else {
                TraceLog(LOG_WARNING, "场景%s的dialog_flow为空", currentSceneId.c_str());
            }
            return;
        }
    }
}

// 1. 对话上下文初始化函数（场景触发成功时调用）
void InitDialogContext(const std::string& scene_id, const RoleDialog& targetRole) {
    // 从全局映射表获取场景对话
    auto it = g_gameData.all_plots.find(scene_id);
    if (it == g_gameData.all_plots.end()) {
        TraceLog(LOG_WARNING, "剧情%s的对话数据不存在，初始化失败", scene_id.c_str());
        return;
    }
    g_gameData.current_plot = scene_id;
    // 初始化上下文状态
    g_gameData.current_scene_dialog = it->second;
    // 取对话流第一个角色
    if (!g_gameData.current_scene_dialog.dialog_flow.empty()) {
        g_gameData.current_role_dialog = targetRole; // 替换：用目标分支角色
        g_gameData.current_line_index = 0; // 重置台词索引为第一句
        g_gameData.is_dialog_showing = true; // 标记对话框显示
        // 加载当前角色的贴图
        const auto& avatarConfig = g_gameData.current_role_dialog.avatar_config;
        // 加载小图
        if (!avatarConfig.small_avatar.empty()) {
            LoadAvatarTexture(avatarConfig.small_avatar);
        }
        // 加载大图
        const auto& bigLayout = avatarConfig.big_layout;
        if (bigLayout.count > 0) {
            for (const auto& imgPath : bigLayout.images) {
                LoadAvatarTexture(imgPath);
            }
        }
        // ==================== 新增：初始化选项列表（适配选项系统） ====================
        if (targetRole.has_choices) {
            g_gameData.current_choices = targetRole.choices;
            TraceLog(LOG_INFO, "初始化选项列表：共%d个选项", targetRole.choices.size());
        }
        else {
            g_gameData.current_choices.clear();
        }
        TraceLog(LOG_INFO, "对话上下文初始化完成：当前角色=%s，第一句台词=%s",
            g_gameData.current_role_dialog.role_name.c_str(),
            g_gameData.current_role_dialog.texts[g_gameData.current_line_index].c_str());
    }
    else {
        TraceLog(LOG_WARNING, "场景%s的dialog_flow为空，无角色可显示", scene_id.c_str());
    }
}

// 2. 对话上下文更新函数（玩家按Z键时调用）
void UpdateDialogContext() {
    if (!g_gameData.is_dialog_showing) return; // 对话框未显示时不处理

    // 优先级1：当前角色还有下一句台词 → 翻台词
    if (g_gameData.current_line_index + 1 < g_gameData.current_role_dialog.texts.size()) {
        g_gameData.current_line_index++;
        TraceLog(LOG_INFO, u8"切换到当前角色第%d句台词：%s",
            g_gameData.current_line_index + 1, g_gameData.current_role_dialog.texts[g_gameData.current_line_index].c_str());
        return;
    }
    
    // 优先级2：当前角色台词说完，有下一个角色 → 切换角色
    if (!g_gameData.current_role_dialog.next_role.empty()) {
        // 查找下一个角色
        bool found = false;
        for (const auto& role : g_gameData.current_scene_dialog.dialog_flow) {
            if (role.role_id == g_gameData.current_role_dialog.next_role) {
                g_gameData.current_role_dialog = role;

                g_gameData.current_line_index = 0;
                // 新增：切换角色时加载选项
                if (role.has_choices) {
                    g_gameData.current_choices = role.choices;
                    g_gameData.selected_choice_index = 0; // 默认选中第一个选项
                }
                else {
                    g_gameData.current_choices.clear();
                    g_gameData.selected_choice_index = -1;
                }
                found = true;
                TraceLog(LOG_INFO, "切换到角色：%s，第一句台词：%s",
                    g_gameData.current_role_dialog.role_name.c_str(),
                    g_gameData.current_role_dialog.texts[g_gameData.current_line_index].c_str());
                break;
            }
        }
        if (!found) {
            TraceLog(LOG_WARNING, "未找到next_role=%s对应的角色，关闭对话", g_gameData.current_role_dialog.next_role.c_str());
            ResetDialogContext(); // 容错：找不到角色则重置
            g_gameData.triggered_plots.insert(g_gameData.current_plot);// 标记为已触发
        }
        return;
    }
    g_gameData.triggered_plots.insert(g_gameData.current_plot);// 标记为已触发
    // 优先级3：无下一个角色 → 关闭对话
    ResetDialogContext();
}

// 对话上下文重置函数（对话关闭时调用）
void ResetDialogContext() {
    g_gameData.is_dialog_showing = false;
    g_gameData.current_scene_dialog = SceneDialog();
    g_gameData.current_role_dialog = RoleDialog();
    g_gameData.current_line_index = 0;
    TraceLog(LOG_INFO, "对话上下文已重置，对话框关闭");
}


// 加载贴图
Texture2D LoadAvatarTexture(const std::string& imgPath) {
    // 空路径直接返回空纹理
    if (imgPath.empty()) {
        return { 0 };
    }
    // 检查缓存
    auto it = g_gameData.avatarTextureCache.find(imgPath);
    if (it != g_gameData.avatarTextureCache.end()) {
        return it->second;
    }
    // 加载新贴图
    Texture2D tex = LoadTexture(imgPath.c_str());
    if (tex.id == 0) {
        TraceLog(LOG_ERROR, "贴图加载失败：%s", imgPath.c_str());
        return { 0 };
    }
    // 存入缓存
    g_gameData.avatarTextureCache[imgPath] = tex;
    TraceLog(LOG_INFO, "成功加载贴图：%s", imgPath.c_str());
    return tex;
}

// 释放所有贴图缓存
void UnloadAllAvatarTextures() {
    for (auto& pair : g_gameData.avatarTextureCache) {
        UnloadTexture(pair.second);
    }
    g_gameData.avatarTextureCache.clear();
    TraceLog(LOG_INFO, "所有贴图缓存已释放");
}

void UnloadCurrentMapResources(std::unordered_map<uint32_t, TilesetData>& tilesetMap) {
    // 1. 释放瓦片集纹理
    for (auto& pair : tilesetMap) {
        UnloadTexture(pair.second.texture);
    }
    tilesetMap.clear();
    // 2. 清空传送点列表
    g_gameData.teleportPoints.clear();
    // 3. 保留玩家精灵（无需释放）
    TraceLog(LOG_INFO, "当前地图资源已卸载");
}

// 对话框绘制函数
void DrawDialogBox(const Font& chineseFont, const Camera2D& camera) {

    // ===================== 可调整的样式参数（核心！改这里就能调样式） =====================
    // 基础样式参数（屏幕像素单位，后续会适配相机缩放）
    const int baseDialogBoxHeight = 280 * g_gameData.scale;                 // 对话框基础高度（屏幕像素）
    const Color dialogBgColor = Fade(BLACK, 0.85f);      // 对话框背景色（半透黑，0.85=透明度，0=全透/1=不透明）
    const int roleNameFontSize = 20 * g_gameData.scale;                     // 角色名字体大小（屏幕像素）
    const Color roleNameColor = RED;                     // 角色名颜色（红色，贴合恐怖RPG风格）
    const int lineFontSize = 20 * g_gameData.scale;                         // 台词字体大小（屏幕像素）
    const Color lineColor = WHITE;                       // 台词颜色（白色，对比半透黑背景更清晰）
    const int hintFontSize = 20 * g_gameData.scale;                         // 翻页提示字体大小（屏幕像素）
    const Color hintColor = YELLOW;                      // 翻页提示颜色（黄色，醒目且不突兀）
    const int baseMargin = 20 * g_gameData.scale;                           // 基础边距（屏幕像素，左右/上下留边）
    const int borderWidth = 5 * g_gameData.scale;                           // 对话框边框宽度（屏幕像素）
    // ====================================================================================

    // ===================== 相机视口坐标计算（核心！适配大地图+相机缩放） =====================
    // 1. 获取屏幕尺寸（基础参考）
    const int screenWidth = GetScreenWidth();            // 游戏窗口屏幕宽度（像素）
    const int screenHeight = GetScreenHeight();          // 游戏窗口屏幕高度（像素）

    // 2. 计算相机缩放比例（适配缩放后的坐标转换）
    const float zoomScale = camera.zoom;                 // 相机缩放比例（比如2.0=放大2倍）

    // 3. 反向计算相机视口在地图坐标系中的真实位置（Camera2D无直接x/y，需通过target/offset推导）
    // 相机视口左上角X（地图坐标系）= 相机跟踪目标X - 屏幕半宽 / 缩放比例
    float cameraScale = CAMERA_SCALE;
    const float cameraViewportX = camera.target.x - (screenWidth / 2) / zoomScale;
    // 相机视口左上角Y（地图坐标系）= 相机跟踪目标Y - 屏幕半高 / 缩放比例
    const float cameraViewportY = camera.target.y - (screenHeight / 2) / zoomScale;
    // 相机视口宽度（地图坐标系）= 屏幕宽度 / 缩放比例
    const float cameraViewportWidth = screenWidth / zoomScale;
    // 相机视口高度（地图坐标系）= 屏幕高度 / 缩放比例
    const float cameraViewportHeight = screenHeight / zoomScale;

    // 4. 计算对话框在地图坐标系中的最终尺寸（适配缩放）
    const float dialogBoxHeight = baseDialogBoxHeight / zoomScale;  // 对话框高度（地图单位，适配缩放）
    const float dialogBoxWidth = cameraViewportWidth - (baseMargin * 2) / zoomScale; // 对话框宽度（左右各留baseMargin）
    const float adaptedBorderWidth = borderWidth / zoomScale;        // 边框宽度（适配缩放）
    const float adaptedMargin = baseMargin / zoomScale;              // 适配后的边距（地图单位）

    // 5. 计算对话框在地图坐标系中的位置（固定在相机视口底部）
    // X坐标：相机视口左边界 + 适配后左边距
    const float dialogBoxX = cameraViewportX + adaptedMargin;
    // Y坐标：相机视口底边界 - 对话框高度 - 适配后下边距（避免超出视口）
    const float dialogBoxY = cameraViewportY + cameraViewportHeight - dialogBoxHeight - adaptedMargin;
    // ====================================================================================

    if (!g_gameData.current_role_dialog.background.empty()) {
        if (g_gameData.current_role_dialog.background == "white") {
            DrawRectangle(cameraViewportX, cameraViewportY, cameraViewportWidth, cameraViewportHeight + 100, Fade(WHITE, 0.95f));

        }
        else if(g_gameData.current_role_dialog.background == "black") {
            DrawRectangle(cameraViewportX, cameraViewportY, cameraViewportWidth, cameraViewportHeight + 100, BLACK);
        }
    }



    const auto& avatarConfig = g_gameData.current_role_dialog.avatar_config;
    // 绘制对话大图
    const auto& bigLayout = avatarConfig.big_layout;
    if (bigLayout.count > 0 && !bigLayout.images.empty()) {
        // 通用参数计算
        const float bigImgHeight = (screenHeight - baseDialogBoxHeight - screenHeight * 0.04) / zoomScale;
        const float bigImgY = (screenHeight * 0.04) / zoomScale + cameraViewportY;
        float perPartWidth = 0.0f;
        std::vector<float> bigImgXList;

        // 按版型计算每份宽度和X坐标
        if (bigLayout.type == "single") {
            perPartWidth = cameraViewportWidth;
            bigImgXList.push_back(cameraViewportX + (cameraViewportWidth - bigImgHeight / 2) / 2); // 居中（按高度等比缩放宽度）
        }
        else if (bigLayout.type == "dual") {
            perPartWidth = cameraViewportWidth / 6.5f;
            bigImgXList.push_back(cameraViewportX + 1 * perPartWidth);
            bigImgXList.push_back(cameraViewportX + 3.5f * perPartWidth);
        }
        else if (bigLayout.type == "triple") {
            perPartWidth = cameraViewportWidth / 10.0f;
            bigImgXList.push_back(cameraViewportX + 1 * perPartWidth);
            bigImgXList.push_back(cameraViewportX + 4 * perPartWidth);
            bigImgXList.push_back(cameraViewportX + 7 * perPartWidth);
        }
        else if (bigLayout.type == "quad") {
            perPartWidth = cameraViewportWidth / 11.0f;
            bigImgXList.push_back(cameraViewportX + 0 * perPartWidth);
            bigImgXList.push_back(cameraViewportX + 3 * perPartWidth);
            bigImgXList.push_back(cameraViewportX + 6 * perPartWidth);
            bigImgXList.push_back(cameraViewportX + 9 * perPartWidth);
        }

        // 绘制每个大图
        for (int i = 0; i < bigLayout.count && i < bigImgXList.size() && i < bigLayout.images.size(); i++) {

            Texture2D bigTex = LoadAvatarTexture(bigLayout.images[i]);
            if (bigTex.id == 0) continue;
            //修正高度
            
            // 按高度等比缩放宽度
           
            
                const float bigImgWidth = (float)bigTex.width / bigTex.height * bigImgHeight;
                Rectangle bigDest = {
                    bigImgXList[i],
                    bigImgY,
                    bigImgWidth,
                    bigImgHeight
                };

                DrawTexturePro(
                    bigTex,
                    { 0, 0, (float)bigTex.width, (float)bigTex.height },
                    bigDest,
                    { 0, 0 },
                    0.0f,
                    WHITE
                );
            
        }

    }
    //*********************************************************************
        // ===================== 对话框区域定义与绘制 =====================
    // 1. 定义对话框矩形区域（地图坐标系，跟随相机移动）
    Rectangle dialogBox = {
        dialogBoxX,          // X坐标：相机视口左留边
        dialogBoxY,          // Y坐标：相机视口底部向上留边
        dialogBoxWidth,      // 宽度：相机视口宽-左右留边
        dialogBoxHeight      // 高度：适配缩放后的固定高度
    };

    // 2. 绘制对话框背景（半透黑，营造恐怖RPG沉浸感）
    DrawRectangleRec(dialogBox, dialogBgColor);
    // 3. 绘制对话框边框（灰色细边框，增加轮廓感，不抢视觉焦点）
    DrawRectangleLinesEx(dialogBox, adaptedBorderWidth, GRAY);
    // ====================================================================================

    // ===================== 角色名绘制 =====================
    // 1. 角色名绘制位置（对话框内左上，留适配后边距）
    Vector2 roleNamePos = {
        dialogBox.x + adaptedMargin,    // X：对话框左边界+适配后左边距
        dialogBox.y + adaptedMargin / 2   // Y：对话框上边界+适配后上边距（减半更紧凑）
    };
    // 2. 容错：确保角色名非空时才绘制（避免空字符串导致的渲染异常）
    if (!g_gameData.current_role_dialog.role_name.empty()) {
        DrawTextEx(
            chineseFont,                        // 中文字体（必须传加载成功的中文支持字体）
            g_gameData.current_role_dialog.role_name.c_str(), // 角色名文本（从上下文变量读取）
            roleNamePos,                        // 绘制位置
            roleNameFontSize,                   // 字体大小（屏幕像素，DrawTextEx自动适配缩放）
            0,                                  // 字符间距（0=默认，正数增大/负数减小）
            roleNameColor                       // 角色名颜色
        );
    }
    // ====================================================================================

    // ===================== 台词绘制 =====================
    // 1. 台词绘制位置（角色名下方）
    Vector2 linePos = {
        dialogBox.x + adaptedMargin,    // X：和角色名对齐
        dialogBox.y + adaptedMargin * 2 + 10 // Y：角色名下方+适配
    };
    // 2. 容错：确保台词索引有效（避免越界访问texts数组）
    if (g_gameData.current_line_index >= 0 && g_gameData.current_line_index < g_gameData.current_role_dialog.texts.size()) {
        DrawTextEx(
            chineseFont,                                        // 中文字体
            g_gameData.current_role_dialog.texts[g_gameData.current_line_index].c_str(), // 当前台词文本（从上下文变量读取）
            linePos,                                            // 绘制位置
            lineFontSize,                                       // 字体大小
            0,                                                  // 字符间距
            lineColor                                           // 台词颜色
        );
    }
    // ====================================================================================

    // ===================== 翻页提示绘制 =====================
    //  翻页提示文本内容
    const char* hintText = u8"F/左键继续 R选择";
    //  计算提示文本的尺寸（用于右对齐定位）
    Vector2 hintSize = MeasureTextEx(
        chineseFont,    // 中文字体（必须和绘制时一致，否则尺寸计算错误）
        hintText,       // 提示文本
        hintFontSize,   // 字体大小
        0               // 字符间距
    );
    //  翻页提示绘制位置（对话框内左下，留适配后边距）
    Vector2 hintPos = {
        dialogBox.x + adaptedMargin,  // X：对话框左边界+左边距
        dialogBox.y + dialogBox.height - hintSize.y - adaptedMargin  // Y：对话框下边界-文本高-下边距
    };
    // 透明度渐变闪烁逻辑（核心）
    const float blinkSpeed = 2.0f; // 闪烁速度（值越大越快，1.0=慢，3.0=快）
    const float minAlpha = 0.3f;   // 最低透明度（0.0=全透，0.3=半透，避免完全看不见）
    const float maxAlpha = 1.0f;   // 最高透明度（1.0=完全显示）
    // 用正弦函数生成0~1的周期值（sin取值[-1,1] → 映射到[0,1]）
    float time = GetTime() * blinkSpeed; // 总时间×速度，控制周期
    float alpha = (sin(time) + 1.0f) / 2.0f; // 映射到0~1
    // 限制透明度范围（minAlpha ~ maxAlpha）
    alpha = minAlpha + (maxAlpha - minAlpha) * alpha;

    // 4. 绘制翻页提示
    DrawTextEx(
        chineseFont,    // 中文字体
        hintText,       // 提示文本
        hintPos,        // 绘制位置
        hintFontSize,   // 字体大小
        0,              // 字符间距
        Fade(hintColor, alpha)       // 渐变颜色
    );
    // ====================================================================================


     // 绘制对话小图
    
    if (!avatarConfig.small_avatar.empty()) {
        Texture2D smallTex = LoadAvatarTexture(avatarConfig.small_avatar);
        if (smallTex.id != 0) {
            // 小图基准尺寸（固定80）
            const float smallSize = 80.0f / zoomScale;
            // 小图位置：对话框右下角
            Rectangle smallDest = {
                dialogBox.x + dialogBoxWidth - smallSize - adaptedMargin,
                dialogBox.y + dialogBox.height - smallSize * 2 - adaptedMargin,
                smallSize,
                smallSize * 2
            };
            DrawTexturePro(
                smallTex,
                { 0, 0, (float)smallTex.width, (float)smallTex.height },
                smallDest,
                { 0, 0 },
                0.0f,
                WHITE
            );
        }
    }








    // ===================== 新增：选项列表绘制 =====================
    if (!g_gameData.current_choices.empty()) {
        // 选项样式参数（原有不变，新增/修改坐标计算）
        const float optionFontSize = 60 / camera.zoom; // 适配缩放
        const float optionMargin = 20 / camera.zoom;   // 选项间距
        const Color normalOptionColor = WHITE;         // 普通选项颜色
        const Color selectedOptionColor = YELLOW;      // 选中选项颜色
        const Color optionBgColor = Fade(GRAY, 0.5f);  // 选项背景色

        // -------------------------- 关键修改：选项框坐标计算 --------------------------
        // 1. 计算相机视口的垂直中间位置（屏幕中间）
        const float cameraViewportMidY = cameraViewportY + cameraViewportHeight / 2.0f;
        // 2. 计算选项框整体高度（所有选项+间距），用于居中对齐
        float totalOptionHeight = 0.0f;
        for (const auto& choice : g_gameData.current_choices) {
            Vector2 optionSize = MeasureTextEx(chineseFont, choice.text.c_str(), optionFontSize, 0);
            totalOptionHeight += optionSize.y + optionMargin;
        }
        totalOptionHeight -= optionMargin; // 最后一个选项去掉多余间距

        // 3. 选项框起始位置：
        // - X：和对话框左对齐（保持视觉统一）
        // - Y：视口中间 - 选项框总高度/2（选项框垂直居中），且再往上偏移（确保在对话框上方）
        // 可调整 offsetY 控制“中间偏上”的幅度（正数=更上，负数=更下）
        const float offsetY = dialogBoxHeight / 2.0f; // 偏移量=对话框高度的一半，确保在对话框上方
        Vector2 optionPos = {
            dialogBox.x + adaptedMargin + cameraViewportWidth / 2 - 70,
            cameraViewportMidY - totalOptionHeight / 2.0f - offsetY // Y：中间-选项高度/2 - 偏移（对话框上方）
        };
        // -------------------------- 坐标计算结束 --------------------------

        // 遍历绘制每个选项（原有逻辑不变）
        for (int i = 0; i < g_gameData.current_choices.size(); i++) {
            const Choice& choice = g_gameData.current_choices[i];
            // 计算选项文本尺寸
            Vector2 optionSize = MeasureTextEx(chineseFont, choice.text.c_str(), optionFontSize, 0);
            // 选项背景矩形（选中时高亮）
            Rectangle optionBg = {
                optionPos.x - optionMargin,
                optionPos.y - optionMargin / 2,
                optionSize.x + optionMargin * 2,
                optionSize.y + optionMargin
            };

            // 绘制选项背景（选中项高亮）
            if (i == g_gameData.selected_choice_index) {
                DrawRectangleRec(optionBg, optionBgColor);
                DrawRectangleLinesEx(optionBg, 2 / camera.zoom, YELLOW);
            }

            // 绘制选项文本
            DrawTextEx(
                chineseFont,
                choice.text.c_str(),
                optionPos,
                optionFontSize,
                0,
                (i == g_gameData.selected_choice_index) ? selectedOptionColor : normalOptionColor
            );

            // 更新下一个选项的Y坐标
            optionPos.y += optionSize.y + optionMargin;
        }
    }
    // ===================== 选项绘制结束 =====================
}

// ============================================================================================
// 传送点系统实现
// ============================================================================================

void CheckTeleportTrigger(
    Player& player,
    tson::Tileson& parser,
    std::unique_ptr<tson::Map>& map,
    std::unordered_map<uint32_t, TilesetData>& tilesetMap,
    tson::Layer*& groundLayer,
    tson::Layer*& collisionLayer,
    tson::Layer*& openedFunituresLayer,
    tson::Layer*& funitureLayer,
    int& tileWidth, int& tileHeight,
    int& mapWidth, int& mapHeight,
    Camera2D& camera) {
    // --- 对话显示时不触发传送 ---
    if (g_gameData.is_dialog_showing) return;

    float currentTime = GetTime();
    // 遍历所有传送点
    for (auto& tp : g_gameData.teleportPoints) {
        //  检测玩家是否进入传送区域
        if (CheckCollisionRecs(player.hitbox, tp.area)) {
            //  2.0s冷却判断
            if (currentTime - g_gameData.last_trigger_time < 2.0f) {
                continue;
            }
            TraceLog(LOG_INFO, "触发传送：%s → %s", g_gameData.current_map_path.c_str(), tp.target_map_path.c_str());
            //  先临时保存传送所需的参数（避免被清空）
            std::string tempMapPath = tp.target_map_path; // 保存路径
            float tempTargetX = tp.target_x;
            float tempTargetY = tp.target_y;
            TraceLog(LOG_INFO, "checktp函数中临时保存的路径：%s", tempMapPath.c_str()); // 验证保存是否正确

            // ===== 先卸载旧瓦片集纹理，防止内存泄漏 =====
            for (auto& pair : tilesetMap) {
                UnloadTexture(pair.second.texture);
            }
            tilesetMap.clear();  // 清空映射表，为新地图腾空间

            // 再加载新地图的所有资源（包括新瓦片集纹理、传送点、对话触发等）
            if (!LoadAllGameResources(parser, map, tilesetMap, player, groundLayer, collisionLayer, openedFunituresLayer, funitureLayer,
                tileWidth, tileHeight, mapWidth, mapHeight, camera, tempMapPath)) {
                TraceLog(LOG_ERROR, "目标地图加载失败，取消传送");
                g_gameData.last_trigger_time = currentTime;
                return;
            }

            // 重置玩家位置到目标点
            player.hitbox.x = tempTargetX;  // 注意：这里用临时保存的变量
            player.hitbox.y = tempTargetY;

            // 重置相机跟随玩家
            camera.target = { player.hitbox.x + player.hitbox.width / 2,
                              player.hitbox.y + player.hitbox.height / 2 };

            // 更新全局游戏时间（用于冷却）
            g_gameData.game_time = currentTime;

            // 标记本次传送已触发冷却
            g_gameData.last_trigger_time = currentTime;

            TraceLog(LOG_INFO, "成功传送至新地图：%s，玩家位置(%f, %f)",
                tempMapPath.c_str(), player.hitbox.x, player.hitbox.y);

            break; // 一次只触发一个传送点
        }
    }
}

// ============================================================================================
// 物品与背包系统实现
// ============================================================================================

// 玩家确认选中的选项（index=选中的选项索引）
void ConfirmSelectedChoice(int index) {
    // ===== 新增：先打印基础日志，定位问题 =====
    TraceLog(LOG_INFO, "===== 进入ConfirmSelectedChoice函数 =====");
    TraceLog(LOG_INFO, "传入的选项索引：%d，当前选项列表数量：%d",
        index, (int)g_gameData.current_choices.size());
    if (index < 0 || index >= g_gameData.current_choices.size()) return;
    Choice selected = g_gameData.current_choices[index];

    // 1. 关键：记录选择值（仅当record_key非空且record_value≠-1时）
    if (!selected.record_key.empty() && selected.record_value != -1) {
        // 匹配PlayerChoices的字段名，写入对应值
        if (selected.record_key == "took_cursed_doll") {
            g_gameData.player_choices.took_cursed_doll = selected.record_value;
			TraceLog(LOG_INFO, "更新玩家选择：took_cursed_doll = %d", selected.record_value);
        }
        else if (selected.record_key == "cabinet_action") {
            g_gameData.player_choices.cabinet_action = selected.record_value;
        }
        else if (selected.record_key == "letter_disposal") {
            g_gameData.player_choices.letter_disposal = selected.record_value;
        }
        else if (selected.record_key == "trusted_villager") {
            g_gameData.player_choices.trusted_villager = selected.record_value;
        }
        else if (selected.record_key == "entered_basement") {
            g_gameData.player_choices.entered_basement = selected.record_value;
        }
        TraceLog(LOG_INFO, "记录选择：%s = %d", selected.record_key.c_str(), selected.record_value);
    }

    // 2. 跳转逻辑（按优先级：target_scene > target_role > 关闭）
    // 优先级1：跨场景跳转
    if (!selected.target_scene.empty()) {
        if (selected.target_scene != "") {
            // 切换到目标场景（先重置对话框状态）
            ResetDialogContext();
            g_gameData.current_map_id = selected.target_scene; // 更新当前场景ID
            CheckSceneDialogTrigger(); // 触发新场景对话
        }
        else {
            // 空字符串=关闭对话框
            ResetDialogContext();
            g_gameData.current_choices.clear();
        }
    }
    // 优先级2：场景内角色跳转
    else if (!selected.target_role.empty()) {
        // 在当前场景dialog_flow中查找target_role的角色
        bool found = false;
        for (const auto& role : g_gameData.current_scene_dialog.dialog_flow) {
            if (role.role_id == selected.target_role) {
                g_gameData.current_role_dialog = role;
                g_gameData.current_line_index = 0;
                // 加载该角色的选项（如果有）
                if (role.has_choices) {
                    g_gameData.current_choices = role.choices;
                    g_gameData.selected_choice_index = 0; // 默认选中第一个选项
                }
                else {
                    g_gameData.current_choices.clear();
                    g_gameData.selected_choice_index = -1;
                }
                found = true;
                TraceLog(LOG_INFO, u8"切换到角色：%s", role.role_name.c_str());
                break;
            }
        }
        if (!found) {
            TraceLog(LOG_WARNING, "未找到角色：%s，关闭对话", selected.target_role.c_str());
            ResetDialogContext();
        }
    }
    // 优先级3：默认关闭对话框
    else {
        ResetDialogContext();
    }
}


void DrawInventoryUI(const Font& chineseFont) {
    // 基础UI参数（和Hover判定里的参数一致，避免不一致）
    const int ITEM_GRID_SIZE = 64*3/2 * g_gameData.scale;
    const int MARGIN = 10 * g_gameData.scale;
    const int START_X = GetScreenWidth()- (5 * (ITEM_GRID_SIZE + MARGIN) + MARGIN)-10 * g_gameData.scale;
    const int START_Y = 10 * g_gameData.scale;
    int screenWidth = GetScreenWidth();
    int screenHeight = GetScreenHeight();
    if (!isWindow)screenHeight = GetMonitorHeight(0);
    //绘制物品说明图片
    if (isItemShowing) {
        DrawRectangle( 0,0,screenWidth,screenHeight,Fade(BLACK,0.6f) );
        int width = screenWidth / 2;
        int height = width / currentShowingItem.istTex.width * currentShowingItem.istTex.height;
        Rectangle ins = {
            (screenWidth - width) / 2,
            (screenHeight - height) / 10,
            width,height
        };
        DrawTexturePro(
            currentShowingItem.istTex,
            { 0,0,(float)currentShowingItem.istTex.width,(float)currentShowingItem.istTex.height },
            ins,
            {0,0},0.0f,
            WHITE
        );
        float fontSize = 32 * g_gameData.scale;
        DrawTextExWithWrap(chineseFont, currentShowingItem.ins,
            { (float)(screenWidth - width) / 2 ,(float)((screenHeight - height) / 10 + height) },
            fontSize, 0.0f, (float)width, YELLOW
        );
        Vector2 mousePos = GetMousePosition();
        Rectangle insR = {
            (screenWidth - width) / 2 + width,0,(screenWidth - width) / 2,screenHeight
        };
        Rectangle insL = {
            0,0,(screenWidth - width) / 2,screenHeight
        };
        if (CheckCollisionPointRec(mousePos, insR) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            isItemShowing = false;
        }
        if (CheckCollisionPointRec(mousePos, insL) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            isItemShowing = false;
        }
        if (IsKeyPressed(KEY_ESCAPE)&&!isInTab) {
            isItemShowing = false;
        }
    }
    // 第一步：绘制物品栏背景
    Rectangle invBg = {
        START_X - MARGIN,
        START_Y - MARGIN,
        5 * (ITEM_GRID_SIZE + MARGIN) + MARGIN, // 5列的总宽度
        2 * (ITEM_GRID_SIZE + MARGIN) + MARGIN  // 2行的总高度
    };
    DrawRectangleRec(invBg, Fade(GRAY, 0.85f)); // 半透灰色背景
    DrawRectangleLinesEx(invBg, 8 * g_gameData.scale, BLACK); // 边框

    // 第二步：绘制每个物品格子
    for (int i = 0; i < 10; i++) {
        Item& item = g_gameData.items[i];
        int row = i / 5;
        int col = i % 5;
        float gridX = START_X + col * (ITEM_GRID_SIZE + MARGIN);
        float gridY = START_Y + row * (ITEM_GRID_SIZE + MARGIN);
        Rectangle gridRect = { gridX, gridY, ITEM_GRID_SIZE, ITEM_GRID_SIZE };
        DrawRectangleLinesEx(gridRect, 4 * g_gameData.scale, BLACK);
        // 绘制格子背景（空格子浅灰，有物品深灰）
        if (item.isExist) {
            DrawRectangleRec(gridRect, Fade(GRAY, 0.5f));
            // 绘制物品图标
            DrawTexturePro(
                item.iconTex,
                { 0,0,(float)item.iconTex.width,(float)item.iconTex.height },
                gridRect,
                { 0,0 },
                0.0f,
                WHITE
            );
            // 绘制物品数量（消耗品）
            if (item.count > 0) {
                std::string countStr = "x" + std::to_string(item.count);
                DrawTextEx(chineseFont, countStr.c_str(), {gridX + ITEM_GRID_SIZE - 20, gridY + ITEM_GRID_SIZE -20}, 24, 0, WHITE);
            }
        }
        else {
            DrawRectangleRec(gridRect, Fade(GRAY, 0.2f));
        }

        //看鼠标位置是否在物品栏上，在就说明

        Vector2 mousePos = GetMousePosition(); // 获取屏幕鼠标坐标（不用转地图坐标，因为UI在屏幕固定位置）
        g_gameData.hoveredItemIndex = -1; // 默认无hover

        // 遍历10个物品格（两排，每排5个）
        for (int i = 0; i < 10; i++) {
            // 计算当前格子的屏幕坐标：前4个第一排，后4个第二排
            int row = i / 5; // 0=第一排，1=第二排
            int col = i % 5; // 0-3=列
            float gridX = START_X + col * (ITEM_GRID_SIZE + MARGIN);
            float gridY = START_Y + row * (ITEM_GRID_SIZE + MARGIN);

            // 定义格子的碰撞矩形
            Rectangle gridRect = { gridX, gridY, ITEM_GRID_SIZE, ITEM_GRID_SIZE };
            // 检测鼠标是否在格子内，且物品存在
            if (CheckCollisionPointRec(mousePos, gridRect) && g_gameData.items[i].isExist) {
                g_gameData.hoveredItemIndex = i; // 记录hover的物品索引
                break; // 一次只hover一个格子
            }
        }


        // 第三步：Hover效果（高亮+显示说明）
        if (g_gameData.hoveredItemIndex == i && item.isExist) {
            // 1. 格子高亮（黄色边框）
            DrawRectangleLinesEx(gridRect, 3, YELLOW);

            // 2. 绘制物品说明框（固定在物品栏左侧）
            Rectangle descBg = {
                START_X - 5 * (ITEM_GRID_SIZE + MARGIN), // 物品栏zuo侧
                START_Y,
                500 * g_gameData.scale, // 说明框宽度
                200 * g_gameData.scale  // 说明框高度
            };
            DrawRectangleRec(descBg, Fade(BLACK, 0.9f));
            DrawRectangleLinesEx(descBg, 2, GRAY);

            // 3. 绘制物品名称和说明
            DrawTextEx(chineseFont, item.name.c_str(), { descBg.x + 10, descBg.y + 10 }, 30, 0, RED);
            DrawTextExWithWrap(chineseFont, item.desc.c_str(), { descBg.x + 10, descBg.y + 40 }, 30, 0, 480 * g_gameData.scale,WHITE);
            
        }

        if (g_gameData.hoveredItemIndex == i && item.isExist && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && item.isInstructionExist && !isItemShowing && !isInTab && !g_gameData.isESC) {
            isItemShowing = true;
            currentShowingItem = item;
        } 
        
    }
}

//===============================带换行的高级文本绘制============================
// 合并版：自动换行绘制中文文字（内置UTF-8完整字符拆分）
void DrawTextExWithWrap(
    const Font& font,        // 你的中文字体
    const std::string& text, // 要显示的文字（物品说明/对话框文字）
    Vector2 startPos,        // 绘制起始坐标
    float fontSize,          // 字号
    float spacing,           // 字间距
    float maxWidth,          // 最大显示宽度（换行阈值）
    Color color              // 文字颜色
) {
    // ========== 第一步：内嵌UTF-8完整字符拆分逻辑 ==========
    std::vector<std::string> charList; // 存储每个完整字符（中文/英文）
    int i = 0;
    while (i < text.size()) {
        // 判断UTF-8字符的字节长度（避免拆分中文）
        int charLen = 1;
        if ((text[i] & 0xF0) == 0xE0) charLen = 3; // 大部分中文占3字节
        else if ((text[i] & 0xE0) == 0xC0) charLen = 2; // 少数中文/符号占2字节
        // 截取完整字符并加入列表
        charList.push_back(text.substr(i, charLen));
        i += charLen;
    }

    // ========== 第二步：按完整字符拆分换行并绘制 ==========
    int charsPerLine = maxWidth / fontSize; // 每行能放的完整字符数
    if (charsPerLine <= 0) charsPerLine = 1; // 避免除数为0

    Vector2 currentPos = startPos; // 当前绘制坐标（逐行偏移Y）
    std::string lineText;          // 临时存储当前行文字
    int currentCharCount = 0;      // 统计当前行的完整字符数

    // 遍历所有完整字符，拼接并绘制
    for (const std::string& c : charList) {
        lineText += c;
        currentCharCount++;

        // 达到每行字符数上限，绘制当前行并重置
        if (currentCharCount >= charsPerLine) {
            DrawTextEx(font, lineText.c_str(), currentPos, fontSize, spacing, color);
            lineText.clear();
            currentCharCount = 0;
            currentPos.y += fontSize + 5; // Y轴下移（行间距=字号+5）
        }
    }

    // 绘制最后一行（不足一行的剩余文字）
    if (!lineText.empty()) {
        DrawTextEx(font, lineText.c_str(), currentPos, fontSize, spacing, color);
    }
}

bool LoadOnceResource(tson::Tileson& parser,Player &player) {
	// ==================== 初始化物品数据结构 ====================
    for (int i = 0; i < 10; i++) {
        g_gameData.items[i].isExist = false;
    }
    // ==================== 解析物品点和互动家具 ====================
    if(!LoadItemsAndInteractions(parser))
        return false;

    //定义所有物品
    allItems.push_back({
        "nameScrip",
        "null",
        u8"小纸条",
        u8"纸条",
        u8"记有一些人名的纸条，死亡笔记吗？有点意思...",
        1,
        LoadTexture("Picture/item/nameScrip.png"),
        true,
        true,
        LoadTexture("Picture/itemInstruction/nameScrip.png"),
        u8"写有几个人人名的名单，不像是物理课代表写的，难道是英语？幸好我早都抄完报纸交了。等等! 怎么还有个绝版的东西？！"
        });


    //初始化地图物品
    InitMapItems();
    //载入存档时间
    InitTab();

    //载入音乐
    InitAudioDevice();
    allMusics.clickEscAndTab = LoadSound("music\\mallet.mp3");
    allMusics.cancle = LoadSound("music\\cuteJump1.wav");
    allMusics.save = LoadSound("music\\save.wav");
    allMusics.getThing = LoadSound("music\\getThing.wav");
    allMusics.tip = LoadSound("music\\tip.wav");
   //载入纹理
   
    if (!LoadAllTextures(player))
        return false;
   
   return true;
}


void drawItemFlash() {
    for (auto item :g_gameData.itemPoints ) {
        if (!item.isPicked && (item.map_id == g_gameData.current_map_id)) {
            float time = GetTime() * 2.0f;  // 2.0f = 渐变速度，越大越快
            unsigned char alpha = (sin(time) + 1) * 127.5f;  // 映射 -1~1 → 0~255
            Color tint = { 255, 255, 255, alpha };  // WHITE底色 + 动态透明度
            Rectangle sourceRec = { 0, 0, (float)allstickers.itemFlash.width, (float)allstickers.itemFlash.height };
            Rectangle destRec = { item.x,item.y,4 * g_gameData.scale,4*g_gameData.scale };
            DrawTexturePro(allstickers.itemFlash, sourceRec, destRec,{0,0}, 0.0f, tint);
        }
    }
    return;
}



void InitMapItems() {
    for (auto& item : g_gameData.itemPoints) {
        for (auto& theItem : allItems) {
            if (item.item_id == theItem.id) {
                item.tip = theItem.mapItemTip;
            }
        }
    }
    return;
}


void CheckPlayerWithMapItemsAndInfo(const Font& chineseFont,const Player& player) {
    for (auto& item : g_gameData.itemPoints) {
        if (!item.isPicked && (item.map_id == g_gameData.current_map_id)) {
            Rectangle itemRec = { item.x - 16,item.y - 16,32,32 };
           
            if (CheckCollisionRecs(player.hitbox, itemRec)) {
                int width = 400* g_gameData.scale;
                int height = 100* g_gameData.scale;
                Rectangle info{ GetScreenWidth() / 2 + 100 * g_gameData.scale ,GetScreenHeight() / 2 - 160 * g_gameData.scale, width,height };
                DrawRectangleRec(info, Fade(GRAY, 0.5f));
                DrawRectangleLinesEx(info, 4, BLACK);
                DrawTextEx(chineseFont, item.tip.c_str(), { (float)(GetScreenWidth() / 2 + 110 * g_gameData.scale) ,(float)(GetScreenHeight() / 2 - 150 * g_gameData.scale )}, 48 * g_gameData.scale, 0, YELLOW);

                if (IsKeyPressed(KEY_E)) {
                    std::string id = item.item_id;
                    for (auto allItem : allItems) {
                        if (allItem.id == id) {
                            
                            for (int i = 0; i < 10; i++) {
                                if (!g_gameData.items[i].isExist) {
                                    g_gameData.items[i] = allItem;
                                    break;
                                }
                            }
                            PlaySound(allMusics.getThing);
                            item.isPicked = true;
                        }
                    }

                }
                
            }

        }
    }
}


void CheckIfLockedDoorCanBeOpened(const Font& chineseFont) {
    Rectangle playerInteractionArea{ g_gameData.player.hitbox.x - 10,g_gameData.player.hitbox.y - 10, 32,32 };
    for (auto& funiture : g_gameData.funitures) {
        if (g_gameData.current_map_id == funiture.map_id && !funiture.isOpened) {
            if (CheckCollisionRecs(playerInteractionArea, funiture.area)) {
                bool hasItem = false;
                for (auto& item : g_gameData.items) {
                    if (item.isExist && item.forDoor_id == funiture.funiture_id) {
                        hasItem = true;
                        int width = 500 * g_gameData.scale;
                        int height = 75 * g_gameData.scale;
                        Rectangle info{ GetScreenWidth() / 2 + 100 * g_gameData.scale ,GetScreenHeight() / 2 - 160 * g_gameData.scale, width,height };
                        DrawRectangleRec(info, Fade(GRAY, 0.5f));
                        DrawRectangleLinesEx(info, 4, BLACK);
                        std::string tip = u8"使用" + item.name;
                        DrawTextEx(chineseFont, tip.c_str(), { GetScreenWidth() / 2 + 110 * (float)g_gameData.scale ,GetScreenHeight() / 2 - 150 * (float)g_gameData.scale }, 64 * g_gameData.scale, 0, GREEN);
                        if (IsKeyPressed(KEY_E)) {
                            funiture.isOpened = true;
                            TraceLog(LOG_INFO, "门已经打开");
                            item.isExist = false;
                            
                        }
                        break;
                    } 
                }
      
                if (!hasItem) {
                    int width = 500 * g_gameData.scale;
                    int height = 200 * g_gameData.scale;
                    Rectangle info{ GetScreenWidth() / 2 + 100 * g_gameData.scale ,GetScreenHeight() / 2 - 160 * g_gameData.scale, width,height };
                    DrawRectangleRec(info, Fade(GRAY, 0.5f));
                    DrawRectangleLinesEx(info, 6, RED);
                    DrawTextExWithWrap(chineseFont, funiture.tip.c_str(), { GetScreenWidth() / 2 + 110 * (float)g_gameData.scale ,GetScreenHeight() / 2 - 150 * (float)g_gameData.scale }, 64 * g_gameData.scale, 0, 480*g_gameData.scale,YELLOW);

                }
               
            }
        }
    }
}


void RenderTab(const Font& font) {
    if (!isInTab)return;

    if (IsKeyPressed(KEY_TAB)&&GetTime()-tabTime > 0.5f) {
        isInTab = false;
        TraceLog(LOG_INFO,("****退出TAB了"));
        isSaving = false;
        isLoading = false;
        showSavingConfirm = false;
        tabTime = GetTime();
        PlaySound(allMusics.cancle);
    }
 
    float saveWidth = 300 * g_gameData.scale;
    float saveHeight = 150 * g_gameData.scale;
    Rectangle save{ GetScreenWidth() / 2 - saveWidth / 2,GetScreenHeight() / 4 ,saveWidth,saveHeight };
    DrawRectangle(save.x, save.y, save.width, save.height, Fade(GREEN, 0.3f));
    DrawRectangleLinesEx(save, 6 * g_gameData.scale, PINK);
    int fontSize = 64*g_gameData.scale;
    const char* Stext = u8"存  档  ";
    int textWidth = MeasureText(Stext, fontSize);
    DrawTextEx(font, Stext, { save.x + (save.width - textWidth) / 2,save.y + (save.height - fontSize) / 2 }, fontSize, 0, PINK);
    Vector2 mousePos = GetMousePosition();


    Rectangle load{ save.x,save.y + saveHeight * 2,saveWidth,saveHeight };
    DrawRectangle(load.x, load.y, save.width, save.height, Fade(GREEN, 0.3f));
    DrawRectangleLinesEx(load, 6 * g_gameData.scale, PINK);
    const char* Ltext = u8"读  档  ";
    DrawTextEx(font, Ltext, { load.x + (save.width - textWidth) / 2,load.y + (save.height - fontSize) / 2 }, fontSize, 0, PINK);
    
    
    //*************************************************************************************
    
    if (!isSaving && !isLoading && currentState==STATE_GAME) {
        if (CheckCollisionPointRec(mousePos, save)) {
            DrawRectangleLinesEx(save, 6 * g_gameData.scale, RED);

            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                isSaving = true;
                tabTime = GetTime();
                PlaySound(allMusics.clickEscAndTab);
            }
        }
        else if(CheckCollisionPointRec(mousePos, load))
        {
            DrawRectangleLinesEx(load, 6 * g_gameData.scale, RED);

            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                isLoading = true;
                tabTime = GetTime();
                PlaySound(allMusics.clickEscAndTab);
            }
        }
    }



    //绘制存档界面
    if (isSaving&&currentState==STATE_GAME) {
        if (IsKeyPressed(KEY_ESCAPE)) {
            isSaving = false;
            PlaySound(allMusics.cancle);
        }
        // ========== 存档选项界面核心绘制 ==========
        // 1. 获取屏幕宽高（动态适配）
        int screenW = GetScreenWidth();
        int screenH = GetScreenHeight();
        DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), Fade(BLACK, 0.6f));

        // 2. 定义主容器（居中，占屏幕1/2宽、全屏高，存档矩形的父容器）
        float containerW = screenW * 0.5f;    // 屏幕宽的1/2
        float containerH = screenH * 1.0f;    // 全屏高
        float containerX = (screenW - containerW) * 0.5f;  // 水平居中
        float containerY = 0.0f;              // 垂直置顶

        // 3. 计算存档矩形的尺寸+间距（均匀分布，自适应屏幕，无硬编码）
        int saveCount = 10;                   // 固定10个存档位
        float containerPadding = containerH * 0.02f;  // 主容器上下内边距（2%高，避免贴边）
        float totalUsableH = containerH - 2 * containerPadding;  // 主容器内可用总高度
        float perItemH = totalUsableH / (saveCount + saveCount - 1);  // 单个矩形高度（10矩形+9间距，均分可用高度）
        float perGapH = perItemH;             // 矩形间距与矩形高度等宽，保证均匀
        float saveRectW = containerW * 0.9f;  // 存档矩形宽度（主容器宽的90%，留左右边距）
        float saveRectH = perItemH;           // 存档矩形高度（动态计算）

        //4.绘制退出选项
        float escRectX = containerX + saveRectW + (containerW - saveRectW) * 0.6f;
        float escRectY = containerY + containerPadding ;
        float length = saveRectW / 20;
        Rectangle escRect{ escRectX,escRectY,length,length };
        DrawRectangleRec(escRect, Fade(GRAY, 0.8f));
        DrawRectangleLinesEx(escRect, 6.0f * g_gameData.scale, BLACK);
        int Xsize = 64*g_gameData.scale;
        DrawText("x", escRectX +  (length-Xsize)/2+14*g_gameData.scale , escRectY + (length - Xsize) / 2, Xsize, RED);
        if (CheckCollisionPointRec(mousePos, escRect)&& !showSavingConfirm) {
            DrawRectangleLinesEx(escRect, 6.0f * g_gameData.scale, YELLOW);
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                isSaving = false;
            }
        }

        // 5. 循环绘制10个存档矩形（index 0-9，从上到下）
        for (int index = 0; index < saveCount; index++)
        {
            // 计算当前存档矩形的位置（水平居中在主容器，垂直均匀分布）
            float saveRectX = containerX + (containerW - saveRectW) * 0.5f;
            float saveRectY = containerY + containerPadding + index * (saveRectH + perGapH);
            Rectangle saveRect = { saveRectX, saveRectY, saveRectW, saveRectH };

            // 6. 判定存档状态
            bool hasSave = false; // 判断第index个存档位是否有存档
            if (savingTime[index] != "null") hasSave = true;
            Color bgColor = hasSave ? Fade(GREEN, 0.7f) : Fade(BLACK, 0.7f); // 有存档=半透绿，无=半透黑
            Color borderColor = BLACK;      // 默认边框色：黑色

            // 7. 鼠标悬停判定：光标在矩形上，边框变黄色
            if (CheckCollisionPointRec(mousePos, saveRect))
            {
                borderColor = YELLOW;
                // 8. 鼠标左键按下：触发确认弹窗，记录选中索引
                if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !showSavingConfirm && GetTime() - tabTime > 0.1f)
                {
                    showSavingConfirm = true;
                    selectedSavingIndex = index;
                    TraceLog(LOG_INFO, "***目前索引是%d", index);
                    tabTime = GetTime();
                }
            }

            // 9. 绘制存档矩形：先填充背景，再画边框（边框宽度6像素，适中）
            DrawRectangleRec(saveRect, bgColor);
            DrawRectangleLinesEx(saveRect, 6.0f * g_gameData.scale, borderColor);

            // 10. 绘制矩形中间的存档时间文本（居中，字体font，大小64，间距0）
            if (hasSave) // 有存档才绘制时间，无存档可省略
            {
                std::string time = savingTime[index];

                // 转换std::string为const char*，测量文本尺寸用于居中
                const char* timeText = time.c_str();
                Vector2 textSize = MeasureTextEx(font, timeText, 48.0f*g_gameData.scale, 0.0f);
                // 计算文本居中坐标（矩形内水平+垂直居中）
                float textX = saveRectX + (saveRectW - textSize.x) / 2.0f;
                float textY = saveRectY + (saveRectH - textSize.y) / 2.0f;
                // 绘制时间文本（白色，可根据需求修改）
                DrawTextEx(font, timeText,{ textX, textY }, 48.0f*g_gameData.scale, 0.0f, WHITE);
            }
        }

        // 11. 确认弹窗绘制：选中存档后显示，居中弹窗+是否选项
        if (showSavingConfirm && selectedSavingIndex != -1 && GetTime() - tabTime > 0.1f)
        {
            // 绘制全屏遮罩（半透黑，遮挡背景，突出弹窗）
            DrawRectangle(0, 0, screenW, screenH, Fade(BLACK, 0.6f));

            // 弹窗容器：居中，宽=主容器80%，高=屏幕20%，浅灰半透背景
            float popupW = containerW * 0.8f;
            float popupH = screenH * 0.2f;
            float popupX = (screenW - popupW) * 0.5f;
            float popupY = (screenH - popupH) * 0.5f;
            Rectangle popupRect = { popupX, popupY, popupW, popupH };
            DrawRectangleRec(popupRect, Fade(LIGHTGRAY, 0.9f));
            DrawRectangleLinesEx(popupRect, 6.0f, BLACK); // 弹窗边框

            // 绘制弹窗提示文本："是否存档/覆盖？"（居中，font，大小48）
            const char* tipText = u8"是否存档/覆盖？";
            Vector2 tipSize = MeasureTextEx(font, tipText, 48.0f*g_gameData.scale, 0.0f);
            float tipX = popupX + (popupW - tipSize.x) / 2.0f;
            float tipY = popupY + popupH * 0.1f; // 顶部留10%间距
            DrawTextEx(font, tipText, { tipX, tipY }, 48.0f*g_gameData.scale, 0.0f, BLACK);

            // 绘制"是/否"按钮：弹窗底部，左右分布，各占30%宽，30%高
            float btnW = popupW * 0.3f;
            float btnH = popupH * 0.3f;
            float btnY = popupY + popupH * 0.6f; // 底部留10%间距
            // 是按钮：左侧
            Rectangle btnYes = { popupX + popupW * 0.15f, btnY, btnW, btnH };
            // 否按钮：右侧
            Rectangle btnNo = { popupX + popupW * 0.55f, btnY, btnW, btnH };

            // 按钮绘制+交互：悬停边框变黄，点击触发对应逻辑
            // --- 是按钮 ---
            Color yesBorder = CheckCollisionPointRec(mousePos, btnYes) ? YELLOW : BLACK;
            DrawRectangleRec(btnYes, Fade(GREEN, 0.7f));
            DrawRectangleLinesEx(btnYes, 8.0f, yesBorder);
            DrawTextEx(font, u8"是", { btnYes.x + (btnW - MeasureTextEx(font, u8"是", 72.0f, 0.0f).x) / 2, btnYes.y + (btnH - 72) / 2 }, 72.0f, 0.0f, WHITE);
            // --- 否按钮 ---
            Color noBorder = CheckCollisionPointRec(mousePos, btnNo) ? YELLOW : BLACK;
            DrawRectangleRec(btnNo, Fade(RED, 0.7f));
            DrawRectangleLinesEx(btnNo, 8.0f, noBorder);
            DrawTextEx(font, u8"否",  { btnNo.x + (btnW - MeasureTextEx(font, u8"否", 72.0f, 0.0f).x) / 2, btnNo.y + (btnH - 72) / 2 }, 72.0f, 0.0f, WHITE);

            // 按钮点击逻辑
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
            {
                if (CheckCollisionPointRec(mousePos, btnYes))
                {
                    // 执行存档/覆盖操作
                    GameElementsPassAndSive(selectedSavingIndex);
                    TraceLog(LOG_INFO, "***执行的索引是%d", selectedSavingIndex);
                    showSavingConfirm = false; // 关闭弹窗
                    PlaySound(allMusics.save);
                    //重载存档时间
                    for (int i = 0; i < 10; i++) {
                        std::string filePath = "saveData\\data_" + std::to_string(i) + ".json";
                        if (FileExists(filePath.c_str())) {
                            std::ifstream is(filePath);
                            json j;
                            is >> j;
                            std::string time = j["save_date"];
                            if (!time.empty()) {
                                savingTime[i] = time;
                                TraceLog(LOG_INFO, "写入存档时间成功，日期为%s", time.c_str());
                            }
                            else {
                                TraceLog(LOG_WARNING, "在重载存档时间时，读取到了JSON但是没有成功写入");
                            }
                        }
                    }
                    selectedSavingIndex = -1;  // 重置选中索引
                }
                else if (CheckCollisionPointRec(mousePos, btnNo))
                {
                    showSavingConfirm = false; // 关闭弹窗，不执行操作
                    selectedSavingIndex = -1;  // 重置选中索引
                }
            }
        }
    }

    //绘制读档界面
    if (isLoading) {
        LoadGame(font);
    }
    

}

// ============================================================================================
// 存档与读档系统实现
// ============================================================================================

void InitTab() {

    //载入存档时间
    for (int i = 0; i < 10; i++) {
        std::string filePath = "saveData\\data_" + std::to_string(i) + ".json";
        if (FileExists(filePath.c_str())) {
            std::ifstream is(filePath);
            json j;
            is >> j;
            std::string time = j["save_date"];
            if (!time.empty()) {
                savingTime[i] = time;
                TraceLog(LOG_INFO, "写入存档时间成功，日期为%s", time.c_str());
            }
        }
    }


}

void GameElementsPassAndSive(int index) {

    // 1. 处理系统时间
    auto t = std::time(nullptr);
    struct tm tm_info;
    localtime_s(&tm_info, &t); // 使用 VS 安全版本
    std::ostringstream oss;
    oss << std::put_time(&tm_info, "%Y-%m-%d %H:%M");
    g_gameData.save_date = oss.str();

    // 2. 序列化
    std::string path = "saveData\\data_" + std::to_string(index) + ".json";
    std::ofstream os(path);
    if (os.is_open()) {
        json j = g_gameData; // 自动处理普通成员

        // 3. 【手动注入】将 vector 塞入 JSON
        // 此时会触发 ItemPoint/LockedFuniture 的宏，只存入 isPicked/isOpened
        j["itemPoints"] = g_gameData.itemPoints;
        j["funitures"] = g_gameData.funitures;

        os << j.dump(4);
        os.close();
        TraceLog(LOG_INFO, "存档成功: %s", path.c_str());
    }

}

void LoadGame(const Font& font) {
    if (IsKeyPressed(KEY_ESCAPE))isLoading = false;
    // ========== 读档选项界面核心绘制（相机外绘制，与存档界面同级） ==========
// 1. 获取屏幕宽高（动态适配，与存档完全一致）
    int screenW = GetScreenWidth();
    int screenH = GetScreenHeight();
    DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), Fade(BLACK, 0.6f));

    // 2. 定义主容器（居中，占屏幕1/2宽、全屏高，与存档完全一致）
    float containerW = screenW * 0.5f;
    float containerH = screenH * 1.0f;
    float containerX = (screenW - containerW) * 0.5f;
    float containerY = 0.0f;

    // 3. 计算读档矩形的尺寸+间距（均匀分布，无硬编码，与存档完全一致）
    int saveCount = 10;
    float containerPadding = containerH * 0.02f;
    float totalUsableH = containerH - 2 * containerPadding;
    float perItemH = totalUsableH / (saveCount + saveCount - 1);
    float perGapH = perItemH;
    float loadRectW = containerW * 0.9f;
    float loadRectH = perItemH;
    Vector2 mousePos = GetMousePosition();

    // 4.绘制退出选项（样式、交互与存档一致，仅修改关闭逻辑为isLoading=false）
    float escRectX = containerX + loadRectW + (containerW - loadRectW) * 0.6f;
    float escRectY = containerY + containerPadding;
    float length = loadRectW / 20;
    Rectangle escRect{ escRectX,escRectY,length,length };
    DrawRectangleRec(escRect, Fade(GRAY, 0.8f));
    DrawRectangleLinesEx(escRect, 6.0f * g_gameData.scale, BLACK);
    int Xsize = 64 * g_gameData.scale;
    DrawText("x", escRectX + (length - Xsize) / 2 + 14 * g_gameData.scale, escRectY + (length - Xsize) / 2, Xsize, RED);
    if (CheckCollisionPointRec(mousePos, escRect) && !showLoadingConfirm) {
        DrawRectangleLinesEx(escRect, 6.0f * g_gameData.scale, YELLOW);
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            isLoading = false; // 关闭读档界面，与存档的isSaving=false对应
            PlaySound(allMusics.cancle);
        }
    }

    // 5. 循环绘制10个读档矩形（index 0-9，从上到下，与存档样式一致，适配读档逻辑）
    for (int index = 0; index < saveCount; index++)
    {
        // 计算当前读档矩形的位置（水平居中，垂直均匀分布，与存档一致）
        float loadRectX = containerX + (containerW - loadRectW) * 0.5f;
        float loadRectY = containerY + containerPadding + index * (loadRectH + perGapH);
        Rectangle loadRect = { loadRectX, loadRectY, loadRectW, loadRectH };

        // 6. 判定存档状态（复用存档时间数组，无存档则无法读档，与存档一致）
        bool hasSave = false;
        if (savingTime[index] != "null") hasSave = true;
        Color bgColor = hasSave ? Fade(GREEN, 0.7f) : Fade(BLACK, 0.7f);
        Color borderColor = BLACK;

        // 7. 鼠标悬停判定：仅有存档时才高亮+响应点击（读档核心区别：无存档不可操作）
        if (CheckCollisionPointRec(mousePos, loadRect) && hasSave)
        {
            borderColor = YELLOW;
            // 8. 鼠标左键按下：防连点+触发读档确认弹窗（与存档一致）
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !showLoadingConfirm && GetTime() - tabTime > 0.1f)
            {
                showLoadingConfirm = true;
                selectedLoadingIndex = index;
                TraceLog(LOG_INFO, "***目前选中读档索引是%d", index);
                tabTime = GetTime();
            }
        }

        // 9. 绘制读档矩形：样式、边框宽度与存档完全一致
        DrawRectangleRec(loadRect, bgColor);
        DrawRectangleLinesEx(loadRect, 6.0f * g_gameData.scale, borderColor);

        // 10. 绘制矩形中间的存档时间文本（有存档才绘制，与存档完全一致）
        if (hasSave)
        {
            std::string time = savingTime[index];
            const char* timeText = time.c_str();
            Vector2 textSize = MeasureTextEx(font, timeText, 48.0f * g_gameData.scale, 0.0f);
            float textX = loadRectX + (loadRectW - textSize.x) / 2.0f;
            float textY = loadRectY + (loadRectH - textSize.y) / 2.0f;
            DrawTextEx(font, timeText, { textX, textY }, 48.0f * g_gameData.scale, 0.0f, WHITE);
        }
    }

    // 11. 读档确认弹窗绘制：样式与存档一致，仅修改提示文字和点击逻辑
    if (showLoadingConfirm && selectedLoadingIndex != -1 && GetTime() - tabTime > 0.1f)
    {
        // 全屏半透遮罩，与存档一致
        DrawRectangle(0, 0, screenW, screenH, Fade(BLACK, 0.6f));

        // 弹窗容器：尺寸、位置、样式与存档完全一致
        float popupW = containerW * 0.8f;
        float popupH = screenH * 0.2f;
        float popupX = (screenW - popupW) * 0.5f;
        float popupY = (screenH - popupH) * 0.5f;
        Rectangle popupRect = { popupX, popupY, popupW, popupH };
        DrawRectangleRec(popupRect, Fade(LIGHTGRAY, 0.9f));
        DrawRectangleLinesEx(popupRect, 6.0f, BLACK);

        // 绘制读档提示文本：替换为“是否读取该存档？”，其余与存档一致
        const char* tipText = u8"是否读取该存档？";
        Vector2 tipSize = MeasureTextEx(font, tipText, 48.0f * g_gameData.scale, 0.0f);
        float tipX = popupX + (popupW - tipSize.x) / 2.0f;
        float tipY = popupY + popupH * 0.1f;
        DrawTextEx(font, tipText, { tipX, tipY }, 48.0f * g_gameData.scale, 0.0f, BLACK);

        // 绘制"是/否"按钮：样式、尺寸、位置与存档完全一致
        float btnW = popupW * 0.3f;
        float btnH = popupH * 0.3f;
        float btnY = popupY + popupH * 0.6f;
        Rectangle btnYes = { popupX + popupW * 0.15f, btnY, btnW, btnH };
        Rectangle btnNo = { popupX + popupW * 0.55f, btnY, btnW, btnH };

        // --- 是按钮绘制+悬停 ---
        Color yesBorder = CheckCollisionPointRec(mousePos, btnYes) ? YELLOW : BLACK;
        DrawRectangleRec(btnYes, Fade(GREEN, 0.7f));
        DrawRectangleLinesEx(btnYes, 8.0f, yesBorder);
        DrawTextEx(font, u8"是", { btnYes.x + (btnW - MeasureTextEx(font, u8"是", 72.0f, 0.0f).x) / 2, btnYes.y + (btnH - 72) / 2 }, 72.0f, 0.0f, WHITE);
        // --- 否按钮绘制+悬停 ---
        Color noBorder = CheckCollisionPointRec(mousePos, btnNo) ? YELLOW : BLACK;
        DrawRectangleRec(btnNo, Fade(RED, 0.7f));
        DrawRectangleLinesEx(btnNo, 8.0f, noBorder);
        DrawTextEx(font, u8"否", { btnNo.x + (btnW - MeasureTextEx(font, u8"否", 72.0f, 0.0f).x) / 2, btnNo.y + (btnH - 72) / 2 }, 72.0f, 0.0f, WHITE);

        // 按钮点击逻辑：适配读档业务，结合isLoading，核心操作写伪代码
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
        {
            if (CheckCollisionPointRec(mousePos, btnYes))
            {
                // ===================== 读档核心操作=====================
                TraceLog(LOG_INFO, "***执行读档索引是%d", selectedLoadingIndex);
                isLoading = true; // 标记为读档中，供你游戏逻辑判断加载状态
                DoLoadGame(selectedLoadingIndex); // 你的读档核心函数：根据索引读取对应存档
                // ==================================================================================
                PlaySound(allMusics.save);
                showLoadingConfirm = false; // 关闭读档弹窗
                selectedLoadingIndex = -1;  // 重置选中索引
                isLoading = false;          // 读档完成后重置加载状态（可根据你的异步加载逻辑调整）
                isInTab = false;
            }
            else if (CheckCollisionPointRec(mousePos, btnNo))
            {
                // 取消读档：仅关闭弹窗，不执行任何操作，与存档一致
                showLoadingConfirm = false;
                selectedLoadingIndex = -1;
            }
        }
    }
}

void DoLoadGame(int index) {
    std::string path = "saveData\\data_" + std::to_string(index) + ".json";;
    std::ifstream is(path);
    if (is.is_open()) {
        json j;
        is >> j;
        // 1. 恢复普通成员 (此时不会触碰 itemPoints 和 funitures)
        j.get_to(g_gameData);

        // 2. 手动局部恢复 itemPoints 
        if (j.contains("itemPoints") && j["itemPoints"].is_array()) {
            auto& jArr = j["itemPoints"];
            // 确保不越界，且顺序一一对应
            size_t size = std::min(jArr.size(), g_gameData.itemPoints.size());
            for (size_t i = 0; i < size; i++) {
                // 关键：对单个元素执行 get_to，只会覆盖宏里指定的 isPicked
                jArr[i].get_to(g_gameData.itemPoints[i]);
            }
        }

        // 3. 手动局部恢复 funitures
        if (j.contains("funitures") && j["funitures"].is_array()) {
            auto& jArr = j["funitures"];
            size_t size = std::min(jArr.size(), g_gameData.funitures.size());
            for (size_t i = 0; i < size; i++) {
                // 关键：只会覆盖 isOpened，保留坐标 area 和 tip 等
                jArr[i].get_to(g_gameData.funitures[i]);
            }
        }
        hasConfirmLoad = true;
        is.close();
        TraceLog(LOG_INFO, "读档成功并完成局部合并: %s", path.c_str());
    }
    else {
        TraceLog(LOG_WARNING, "********读档时打不开存档");
    }



        for (int i = 0; i < 10; i++) {
            std::string id = g_gameData.items[i].id;
            if (g_gameData.items[i].isExist)
                for (auto allItem : allItems) {
                    if (allItem.id == id) {
                        int temp = g_gameData.items[i].count;
                        g_gameData.items[i] = allItem;
                        g_gameData.items[i].count = temp;
                        break;
                    }
                }
        }

            //tiaoshi***************
            for (auto Mitem : g_gameData.itemPoints) {
                TraceLog(LOG_INFO, "***读档后物品点ID：%s", Mitem.item_id.c_str());
                if (Mitem.isPicked) {
                    TraceLog(LOG_INFO, "***此物品已经捡拾");
                }
                else {
                    TraceLog(LOG_INFO, "***此物品没有没有没有捡拾");
                }
                if (!Mitem.map_id.empty()) {
                    TraceLog(LOG_INFO, "***此物品的地图id是%s",Mitem.map_id.c_str());
                }
                else {
                    TraceLog(LOG_INFO, "***此物品的地图字柄被清除了");
                }
            
            }
    
            currentState = STATE_GAME;
}




// ============================================================================================
// AI 与 A* 寻路系统实现
// ============================================================================================

std::vector<Vector2> FindPath(Vector2 startWorld, Vector2 goalWorld,
    Rectangle monsterHitbox,
    tson::Layer* collisionLayer, int tw, int th) {
    auto DistSq = [](Vector2 a, Vector2 b) -> float {
        float dx = a.x - b.x;
        float dy = a.y - b.y;
        return dx * dx + dy * dy;
        };

    auto WorldToTile = [](Vector2 world, int tileSize) -> Vector2 {
        return { floorf(world.x / tileSize), floorf(world.y / tileSize) };
        };
    auto TileToWorldCenter = [](Vector2 tile, int tileSize) -> Vector2 {
        return { tile.x * tileSize + tileSize / 2.0f, tile.y * tileSize + tileSize / 2.0f };
        };

    int tileSize = tw;
    Vector2 startTile = WorldToTile(startWorld, tileSize);
    Vector2 goalTile = WorldToTile(goalWorld, tileSize);

    if (startTile.x == goalTile.x && startTile.y == goalTile.y) {
        return {}; // 同格
    }

    auto IsTileWalkable = [&](Vector2 tile) -> bool {
        Vector2 testWorld = TileToWorldCenter(tile, tileSize);
        return CanMoveTo(testWorld, monsterHitbox, collisionLayer, tw, th);
        };

    if (!IsTileWalkable(goalTile)) return {};

    // 所有节点存储在这里，自动管理内存
    std::vector<AStarNode> nodes;
    nodes.reserve(500); // 根据地图大小预分配，避免频繁 realloc

    // openSet：存储节点索引，按 f 值最小堆
    auto comparator = [&](int lhs, int rhs) { return nodes[lhs] > nodes[rhs]; };
    std::priority_queue<int, std::vector<int>, decltype(comparator)> openSet(comparator);

    // 记录瓦片坐标到节点索引的映射（快速查找是否已访问）
    std::unordered_map<int64_t, int> tileToIndex;  // key = x * 10000 + y（假设地图不大）
    auto TileKey = [](int x, int y) { return static_cast<int64_t>(x) * 10000 + y; };

    // closedSet
    std::unordered_set<int64_t> closedSet;

    // 添加起点
    nodes.emplace_back();
    nodes.back().pos = TileToWorldCenter(startTile, tileSize);
    nodes.back().g = 0.0f;
    nodes.back().h = DistSq(nodes.back().pos, goalWorld);
    nodes.back().f = nodes.back().h;
    nodes.back().parentIndex = -1;

    int startIndex = 0;
    openSet.push(startIndex);
    tileToIndex[TileKey((int)startTile.x, (int)startTile.y)] = startIndex;

    std::vector<Vector2> directions = { {0, -1}, {0, 1}, {-1, 0}, {1, 0} }; // 四方向

    int goalIndex = -1;

    while (!openSet.empty()) {
        int currentIndex = openSet.top();
        openSet.pop();

        AStarNode& current = nodes[currentIndex];
        Vector2 currentTile = WorldToTile(current.pos, tileSize);
        int64_t currentKey = TileKey((int)currentTile.x, (int)currentTile.y);

        closedSet.insert(currentKey);

        if (currentTile.x == goalTile.x && currentTile.y == goalTile.y) {
            goalIndex = currentIndex;
            break;
        }

        for (auto& dir : directions) {
            Vector2 neighborTile = { currentTile.x + dir.x, currentTile.y + dir.y };
            int64_t neighborKey = TileKey((int)neighborTile.x, (int)neighborTile.y);

            if (closedSet.count(neighborKey)) continue;
            if (!IsTileWalkable(neighborTile)) continue;

            float tentativeG = current.g + tileSize;

            auto it = tileToIndex.find(neighborKey);
            int neighborIndex;
            if (it == tileToIndex.end()) {
                // 新节点
                nodes.emplace_back();
                neighborIndex = static_cast<int>(nodes.size() - 1);
                tileToIndex[neighborKey] = neighborIndex;

                AStarNode& neighbor = nodes.back();
                neighbor.pos = TileToWorldCenter(neighborTile, tileSize);
                neighbor.g = tentativeG;
                neighbor.h = DistSq(neighbor.pos, goalWorld);
                neighbor.f = tentativeG + neighbor.h;
                neighbor.parentIndex = currentIndex;

                openSet.push(neighborIndex);
            }
            else {
                // 已存在，检查是否更好路径
                neighborIndex = it->second;
                AStarNode& neighbor = nodes[neighborIndex];
                if (tentativeG < neighbor.g) {
                    neighbor.g = tentativeG;
                    neighbor.f = tentativeG + neighbor.h;
                    neighbor.parentIndex = currentIndex;
                    openSet.push(neighborIndex); // 重新入堆（priority_queue 不支持 decrease-key，直接再推）
                }
            }
        }
    }

    // 重建路径
    std::vector<Vector2> path;
    if (goalIndex != -1) {
        int curr = goalIndex;
        while (nodes[curr].parentIndex != -1) {
            path.push_back(nodes[curr].pos);
            curr = nodes[curr].parentIndex;
        }
        std::reverse(path.begin(), path.end());
    }

    return path; // vector 自动销毁 nodes，无泄漏
}


// 新 UpdateMonsterAI（完全重写）
void UpdateMonsterAI(Monster& monster, Player& player, tson::Layer* collisionLayer, int tw, int th) {
    if (!monster.active) return;
	if (g_gameData.isInUi) return;

    float dt = GetFrameTime();
    Vector2 monsterPos = { monster.hitbox.x + monster.hitbox.width / 2.0f,
                           monster.hitbox.y + monster.hitbox.height / 2.0f }; // 用中心
    Vector2 playerPos = { player.hitbox.x + player.hitbox.width / 2.0f,
                          player.hitbox.y + player.hitbox.height / 2.0f };

    // --- 路径重算逻辑 ---
    bool needRecalc = false;
    if (monster.path.empty()) needRecalc = true; // 首次或路径用完
    if (DistSq(playerPos, monster.lastPlayerPosForPath) > monster.pathRecalcDistSq) needRecalc = true;
    monster.recalcTimer -= dt;
    if (monster.recalcTimer <= 0.0f) needRecalc = true;

    if (needRecalc) {
        monster.path = FindPath(monsterPos, playerPos, monster.hitbox, collisionLayer, tw, th);
        monster.currentPathIndex = 0;
        monster.recalcTimer = monster.recalcInterval;
        monster.lastPlayerPosForPath = playerPos;

        // 如果路径为空，直接朝玩家方向（罕见极端情况）
        if (monster.path.empty()) {
            float dx = playerPos.x - monsterPos.x;
            float dy = playerPos.y - monsterPos.y;
            if (fabs(dx) > fabs(dy)) monster.currentDir = (dx > 0) ? DIR_RIGHT : DIR_LEFT;
            else monster.currentDir = (dy > 0) ? DIR_DOWN : DIR_UP;
        }
    }

    // --- 移动逻辑：跟随路径 ---
    Vector2 targetPos = playerPos; // 默认直奔玩家（路径为空时）
    if (!monster.path.empty() && monster.currentPathIndex < monster.path.size()) {
        targetPos = monster.path[monster.currentPathIndex];

        // 到达当前路径点 → 切换下一个
        if (DistSq(monsterPos, targetPos) < 12.0f * 12.0f) { // 接近阈值
            monster.currentPathIndex++;
        }
    }

    // 计算方向并移动
    Vector2 dirVec = { targetPos.x - monsterPos.x, targetPos.y - monsterPos.y };
    float distToTarget = sqrtf(dirVec.x * dirVec.x + dirVec.y * dirVec.y);
    if (distToTarget > 1.0f) {
        dirVec.x /= distToTarget;
        dirVec.y /= distToTarget;

        // 更新朝向
        if (fabs(dirVec.x) > fabs(dirVec.y)) {
            monster.currentDir = (dirVec.x > 0) ? DIR_RIGHT : DIR_LEFT;
        }
        else {
            monster.currentDir = (dirVec.y > 0) ? DIR_DOWN : DIR_UP;
        }

        Vector2 vel = { dirVec.x * monster.speed, dirVec.y * monster.speed };
        Vector2 nextPos = { monster.hitbox.x + vel.x * dt, monster.hitbox.y + vel.y * dt };

        // 墙滑
        if (CanMoveTo(nextPos, monster.hitbox, collisionLayer, tw, th)) {
            monster.hitbox.x = nextPos.x;
            monster.hitbox.y = nextPos.y;
        }
        else {
            // 尝试只X或只Y
            Vector2 posX = { monster.hitbox.x + vel.x * dt, monster.hitbox.y };
            Vector2 posY = { monster.hitbox.x, monster.hitbox.y + vel.y * dt };
            if (CanMoveTo(posX, monster.hitbox, collisionLayer, tw, th)) {
                monster.hitbox.x = posX.x;
            }
            else if (CanMoveTo(posY, monster.hitbox, collisionLayer, tw, th)) {
                monster.hitbox.y = posY.y;
            }
        }

        // 动画（只在移动时）
        monster.frameCounter++;
        if (monster.frameCounter >= monster.frameInterval) {
            monster.currentFrame = (monster.currentFrame + 1) % 4;
            monster.frameCounter = 0;
        }
    }
}

inline float DistSq(Vector2 a, Vector2 b) {
    float dx = a.x - b.x;
    float dy = a.y - b.y;
    return dx * dx + dy * dy;
}









bool Loadmaps(
    tson::Tileson& parser,
    std::unique_ptr<tson::Map>& map,
    std::unordered_map<uint32_t, TilesetData>& tilesetMap,
    tson::Layer*& groundLayer,
    tson::Layer*& collisionLayer,
    tson::Layer*& openedFunituresLayer,
    tson::Layer*& funitureLayer,
    int& tileWidth, int& tileHeight,
    int& mapWidth, int& mapHeight,
    Camera2D& camera,
    std::string mapPath) {
    map = parser.parse(mapPath); // 通用加载：通过路径加载对应地图
    if (!map) {
        TraceLog(LOG_ERROR, "地图加载失败：无法解析文件 %s", mapPath.c_str());
        CloseWindow();
        return false;
    }
    if (map->getStatus() != tson::ParseStatus::OK)
    {
        TraceLog(LOG_ERROR, "地图加载失败！路径：%s，错误信息：%s", mapPath.c_str(), map->getStatusMessage().c_str());
        CloseWindow();
        return false;
    }

    // ==================== 读取地图的map_id属性 ====================
    //  1.4.0标准写法：获取属性集合（类名是PropertyCollection）
    tson::PropertyCollection& mapProps = map->getProperties();
    //  从集合中获取单个map_id属性
    tson::Property* mapIdProp = mapProps.getProperty("map_id");
    //  验证属性存在 + 类型为字符串
    if (mapIdProp != nullptr) {
        // 先判断属性类型是String
        if (mapIdProp->getType() == tson::Type::String) {
            // 替换getStringValue()为getValue<std::string>()（1.4.0的标准写法）
            g_gameData.current_map_id = mapIdProp->getValue<std::string>();
            TraceLog(LOG_INFO, "成功读取地图map_id：%s", g_gameData.current_map_id.c_str());
        }
        else {
            g_gameData.current_map_id = "UNKNOWN";
            TraceLog(LOG_WARNING, " map_id属性需设为String类型");
        }
    }
    else {
        g_gameData.current_map_id = "UNKNOWN";
        TraceLog(LOG_WARNING, "地图未标注map_id属性");
    }

    // ==================== 记录当前地图路径 ====================
    g_gameData.current_map_path = mapPath;
    // 清空上一地图的传送点（避免残留）
    g_gameData.teleportPoints.clear();

    // 加载对话JSON
    if (!LoadDialogsFromJSON("dialogs\\startDialog.json")) {
        TraceLog(LOG_ERROR, "对话JSON加载失败，游戏退出");
        CloseWindow();
        return false;
    }

    // 触发对话检测
    CheckSceneDialogTrigger();

    tileWidth = map->getTileSize().x;
    tileHeight = map->getTileSize().y;
    mapWidth = map->getSize().x;
    mapHeight = map->getSize().y;
    TraceLog(LOG_INFO, "地图大小：%d x %d 格子，每个格子 %d x %d 像素",
        mapWidth, mapHeight, tileWidth, tileHeight);

    // -------------------- 遍历所有瓦片集，加载每个集的纹理和参数 --------------------
    auto& tilesets = map->getTilesets();
    if (tilesets.empty()) {
        TraceLog(LOG_ERROR, "地图未绑定任何瓦片集！");
        CloseWindow();
        return false;
    }
    for (auto& ts : tilesets) { // 遍历所有瓦片集
        TilesetData tsData;
        tsData.tileset = &ts;
        tsData.firstGid = ts.getFirstgid();
        tsData.columns = ts.getColumns();

        // 调试：打印新地图瓦片集的关键信息**********************************
        TraceLog(LOG_INFO, "新地图瓦片集：firstGid=%d，图片路径=%s",
            tsData.firstGid, ts.getImagePath().string().c_str());

        // 加载当前瓦片集的纹理
        std::string imagePath = ts.getImagePath().string();
        tsData.texture = LoadTexture(imagePath.c_str());
        if (tsData.texture.id == 0) {
            TraceLog(LOG_ERROR, "瓦片集加载失败！路径：%s", imagePath.c_str());
            CloseWindow();
            return false;
        }
        else {

            TraceLog(LOG_INFO, "成功加载新地图瓦片集：firstGid=%d，存入tilesetMap", tsData.firstGid);
        }
        // 存入映射表（key=firstGid，方便后续查找）
        tilesetMap[tsData.firstGid] = tsData;
        TraceLog(LOG_INFO, "加载瓦片集：firstGid=%d，列数=%d，纹理路径=%s",
            tsData.firstGid, tsData.columns, imagePath.c_str());
    }

    // ==================== 解析传送点 ====================
    tson::Layer* teleportLayer = map->getLayer("TeleportPoints");
    if (teleportLayer && teleportLayer->getType() == tson::LayerType::ObjectGroup) {
        TraceLog(LOG_INFO, "找到TeleportPoints对象层，包含%d个对象", teleportLayer->getObjects().size());
        for (auto& obj : teleportLayer->getObjects()) {
            if (obj.getType() == "rectangle" || obj.getType().empty()) {
                TraceLog(LOG_INFO, "解析矩形对象：x=%.1f, y=%.1f, w=%.1f, h=%.1f",
                    obj.getPosition().x + 0.0f, obj.getPosition().y + 0.0f, obj.getSize().x + 0.0f, obj.getSize().y + 0.0f);
                TeleportPoint tp;
                // 1. 解析触发区域（JSON的x/y/width/height是像素，直接用）
                tson::Vector2 objPos = obj.getPosition();   // 获取左上角坐标（x/y）
                tson::Vector2 objSize = obj.getSize();     // 获取尺寸（width/height）
                tp.area = { objPos.x + 0.0f, objPos.y + 0.0f, objSize.x + 0.0f, objSize.y + 0.0f };
                g_gameData.last_trigger_time = 0.0f; // 初始冷却

                // 2. 解析自定义属性
                auto& props = obj.getProperties();
                // 解析target_map（补全路径：Tile/xxx.json）
                if (props.hasProperty("target_map")) {
                    std::string targetMapName = props.getProperty("target_map")->getValue<std::string>();
                    tp.target_map_path = "Tile\\" + targetMapName + ".json"; // 补全路径


                }
                else {
                    TraceLog(LOG_WARNING, "传送点(%f,%f)缺少target_map属性，跳过", objPos.x + 0.0f, objPos.y + 0.0f);
                    continue;
                }
                // 解析target_x/target_y（格子数→像素）
                if (props.hasProperty("target_x") && props.hasProperty("target_y")) {
                    int targetXGrid = props.getProperty("target_x")->getValue<int>();
                    int targetYGrid = props.getProperty("target_y")->getValue<int>();
                    tp.target_x = targetXGrid * tileWidth; // 格子→像素
                    tp.target_y = targetYGrid * tileHeight;
                }
                else {
                    TraceLog(LOG_WARNING, "传送点(%f,%f)缺少target_x/y属性，跳过", objPos.x + 0.0f, objPos.y + 0.0f);
                    continue;
                }

                // 3. 加入传送点列表
                g_gameData.teleportPoints.push_back(tp);
                TraceLog(LOG_INFO, "解析传送点：区域(%f,%f,%f,%f) → 目标地图%s，目标位置(%f,%f)",
                    tp.area.x, tp.area.y, tp.area.width, tp.area.height,
                    tp.target_map_path.c_str(), tp.target_x, tp.target_y);
            }
        }
    }
    else {
        TraceLog(LOG_INFO, "未找到TeleportPoints图层，当前地图无传送点");
    }
    // 找到图层
    groundLayer = map->getLayer("Ground");
    if (!groundLayer || groundLayer->getType() != tson::LayerType::TileLayer)
    {
        TraceLog(LOG_ERROR, "找不到名为 Ground 的 TileLayer！");
        for (auto& pair : tilesetMap) {
            UnloadTexture(pair.second.texture);
        }
        return false;
    }
    collisionLayer = map->getLayer("Collision");
    if (collisionLayer->getType() != tson::LayerType::TileLayer)
    {
        TraceLog(LOG_ERROR, "找不到名为 Collision 的 TileLayer！");
        for (auto& pair : tilesetMap) {
            UnloadTexture(pair.second.texture);
        }
        return false;
    }

    //加载OpenedFunitures图层
    openedFunituresLayer = map->getLayer("OpenedFunitures");
    if (openedFunituresLayer) {
        TraceLog(LOG_INFO, "找到OpenedFunitures图层");

    }
    else {
        TraceLog(LOG_WARNING, "没有到OpenedFunitures图层");
    }

    //载入Funiture图层
    funitureLayer = map->getLayer("Funiture");
    if (funitureLayer) {
        TraceLog(LOG_INFO, "找到Funiture图层");
    }
    else {
        TraceLog(LOG_WARNING, "未找到Funiture图层");
    }

	return true;
}

bool LoadAllTextures(Player &player) {
    // 加载16张玩家独立动画帧
    std::string basePath = "Picture\\GameCharacter\\";
    // 1. 下方向4帧
    player.sprites[DIR_DOWN][0] = LoadTexture((basePath + "player_down_0.png").c_str());
    player.sprites[DIR_DOWN][1] = LoadTexture((basePath + "player_down_1.png").c_str());
    player.sprites[DIR_DOWN][2] = LoadTexture((basePath + "player_down_2.png").c_str());
    player.sprites[DIR_DOWN][3] = LoadTexture((basePath + "player_down_3.png").c_str());
    // 2. 上方向4帧
    player.sprites[DIR_UP][0] = LoadTexture((basePath + "player_up_0.png").c_str());
    player.sprites[DIR_UP][1] = LoadTexture((basePath + "player_up_1.png").c_str());
    player.sprites[DIR_UP][2] = LoadTexture((basePath + "player_up_2.png").c_str());
    player.sprites[DIR_UP][3] = LoadTexture((basePath + "player_up_3.png").c_str());
    // 3. 左方向4帧
    player.sprites[DIR_LEFT][0] = LoadTexture((basePath + "player_left_0.png").c_str());
    player.sprites[DIR_LEFT][1] = LoadTexture((basePath + "player_left_1.png").c_str());
    player.sprites[DIR_LEFT][2] = LoadTexture((basePath + "player_left_2.png").c_str());
    player.sprites[DIR_LEFT][3] = LoadTexture((basePath + "player_left_3.png").c_str());
    // 4. 右方向4帧
    player.sprites[DIR_RIGHT][0] = LoadTexture((basePath + "player_right_0.png").c_str());
    player.sprites[DIR_RIGHT][1] = LoadTexture((basePath + "player_right_1.png").c_str());
    player.sprites[DIR_RIGHT][2] = LoadTexture((basePath + "player_right_2.png").c_str());
    player.sprites[DIR_RIGHT][3] = LoadTexture((basePath + "player_right_3.png").c_str());

    if (player.sprites[DIR_DOWN][0].id == 0) {
        TraceLog(LOG_ERROR, "玩家精灵加载失败！");
        return false;
    }
    //==================== 加载怪物纹理 ====================
    std::string basePathm = "Picture\\GameCharacter\\";
    // 1. 下方向4帧
    g_gameData.monsterJ.sprites[DIR_DOWN][0] = LoadTexture((basePathm + "monster_down_0.png").c_str());
    g_gameData.monsterJ.sprites[DIR_DOWN][1] = LoadTexture((basePathm + "monster_down_1.png").c_str());
    g_gameData.monsterJ.sprites[DIR_DOWN][2] = LoadTexture((basePathm + "monster_down_2.png").c_str());
    g_gameData.monsterJ.sprites[DIR_DOWN][3] = LoadTexture((basePathm + "monster_down_3.png").c_str());
    // 2. 上方向4帧
    g_gameData.monsterJ.sprites[DIR_UP][0] = LoadTexture((basePathm + "monster_up_0.png").c_str());
    g_gameData.monsterJ.sprites[DIR_UP][1] = LoadTexture((basePathm + "monster_up_1.png").c_str());
    g_gameData.monsterJ.sprites[DIR_UP][2] = LoadTexture((basePathm + "monster_up_2.png").c_str());
    g_gameData.monsterJ.sprites[DIR_UP][3] = LoadTexture((basePathm + "monster_up_3.png").c_str());
    // 3. 左方向4帧
    g_gameData.monsterJ.sprites[DIR_LEFT][0] = LoadTexture((basePathm + "monster_left_0.png").c_str());
    g_gameData.monsterJ.sprites[DIR_LEFT][1] = LoadTexture((basePathm + "monster_left_1.png").c_str());
    g_gameData.monsterJ.sprites[DIR_LEFT][2] = LoadTexture((basePathm + "monster_left_2.png").c_str());
    g_gameData.monsterJ.sprites[DIR_LEFT][3] = LoadTexture((basePathm + "monster_left_3.png").c_str());
    // 4. 右方向4帧
    g_gameData.monsterJ.sprites[DIR_RIGHT][0] = LoadTexture((basePathm + "monster_right_0.png").c_str());
    g_gameData.monsterJ.sprites[DIR_RIGHT][1] = LoadTexture((basePathm + "monster_right_1.png").c_str());
    g_gameData.monsterJ.sprites[DIR_RIGHT][2] = LoadTexture((basePathm + "monster_right_2.png").c_str());
    g_gameData.monsterJ.sprites[DIR_RIGHT][3] = LoadTexture((basePathm + "monster_right_3.png").c_str());

    if (g_gameData.monsterJ.sprites[DIR_DOWN][0].id == 0) {
        TraceLog(LOG_ERROR, "怪物精灵加载失败！");
        // 释放已加载的瓦片集纹理
        CloseWindow();
        return false;
    }

    //加载背景等贴图纹理
    allstickers.menuBg = LoadTexture("Picture\\Other\\menu_bg.png");
    allstickers.escBg = LoadTexture("Picture\\Character\\exit_bg.png");
    allstickers.tabBg = LoadTexture("Picture\\Other\\tab_bg.png");
    allstickers.itemFlash = LoadTexture("Picture\\Effect\\flash.png");

	return true;
}

bool LoadItemsAndInteractions(tson::Tileson& parser) {
    for (auto& theMap : allMaps) {
        std::unique_ptr<tson::Map> map;
        std::string mapPath = "Tile\\" + theMap + ".json";
        map = parser.parse(mapPath);
        if (!map) {
            TraceLog(LOG_WARNING, "cant load map to load itemPoint");
            return false;
        }
        ItemPoint  item;
        tson::PropertyCollection& mapProps = map->getProperties();
        //  从集合中获取单个map_id属性
        tson::Property* mapIdProp = mapProps.getProperty("map_id");
        //  验证属性存在 + 类型为字符串
        if (mapIdProp != nullptr) {
            // 先判断属性类型是String
            if (mapIdProp->getType() == tson::Type::String) {
                // 替换getStringValue()为getValue<std::string>()（1.4.0的标准写法）

                TraceLog(LOG_INFO, "成功读取地图map_id：%s", g_gameData.current_map_id.c_str());
            }

        }

        tson::Layer* pointLayer = map->getLayer("ItemPoints");
        if (pointLayer && pointLayer->getType() == tson::LayerType::ObjectGroup) {
            TraceLog(LOG_INFO, "找到ItemPoints对象层，包含%d个对象", pointLayer->getObjects().size());
            item.map_id = mapIdProp->getValue<std::string>();
            for (auto& obj : pointLayer->getObjects()) {
                if (obj.getType() == "point" || obj.getType().empty()) {
                    TraceLog(LOG_INFO, "解析点对象：x=%d, y=%d",
                        obj.getProperties().getProperty("x")->getValue<int>(), obj.getProperties().getProperty("y")->getValue<int>());

                    item.x = obj.getProperties().getProperty("x")->getValue<int>();
                    item.y = obj.getProperties().getProperty("y")->getValue<int>();


                    auto& props = obj.getProperties();
                    // 解析item_id
                    if (props.hasProperty("item_id")) {
                        std::string item_id = props.getProperty("item_id")->getValue<std::string>();
                        item.item_id = item_id;
                        TraceLog(LOG_INFO, "物品id为%s", item_id.c_str());

                    }

                    //  加入物品点列表
                    g_gameData.itemPoints.push_back(item);
                    TraceLog(LOG_INFO, "加入物品列表");
                }
            }
        }
        else {
            TraceLog(LOG_INFO, "地图%s不含物品点", theMap.c_str());
        }
    }
    //============================解析地图交互门区块===========================================
    for (auto theMap : allMaps) {
        std::unique_ptr<tson::Map> map;
        std::string mapPath = "Tile\\" + theMap + ".json";
        map = parser.parse(mapPath);
        if (!map) {
            TraceLog(LOG_WARNING, "cant load map to load itemPoint");
            return false;
        }
        LockedFuniture funiture;//定义家具
        tson::PropertyCollection& mapProps = map->getProperties();
        //  从集合中获取单个map_id属性
        tson::Property* mapIdProp = mapProps.getProperty("map_id");
        //  验证属性存在 + 类型为字符串
        if (mapIdProp != nullptr) {
            // 先判断属性类型是String
            if (mapIdProp->getType() == tson::Type::String) {
                std::string map_id = mapIdProp->getValue<std::string>();
                funiture.map_id = map_id;
                TraceLog(LOG_INFO, "在交互家具函数成功读取地图map_id");
            }

        }
        tson::Layer* funitureLayer = map->getLayer("LockedFunitures");
        if (funitureLayer && funitureLayer->getType() == tson::LayerType::ObjectGroup) {
            TraceLog(LOG_INFO, "找到LockedFunitures对象层，包含%d个对象", funitureLayer->getObjects().size());
            for (auto& obj : funitureLayer->getObjects()) {
                if (obj.getType() == "rectangle" || obj.getType().empty()) {
                    TraceLog(LOG_INFO, "解析LF矩形对象：x=%.1f, y=%.1f, w=%.1f, h=%.1f",
                        obj.getPosition().x + 0.0f, obj.getPosition().y + 0.0f, obj.getSize().x + 0.0f, obj.getSize().y + 0.0f);

                    // 1. 解析触发区域（JSON的x/y/width/height是像素，直接用）
                    tson::Vector2 objPos = obj.getPosition();   // 获取左上角坐标（x/y）
                    tson::Vector2 objSize = obj.getSize();     // 获取尺寸（width/height）
                    funiture.area = { objPos.x + 0.0f, objPos.y + 0.0f, objSize.x + 0.0f, objSize.y + 0.0f };


                    // 2. 解析自定义属性
                    auto& props = obj.getProperties();

                    if (props.hasProperty("door_id")) {
                        std::string door_id = props.getProperty("door_id")->getValue<std::string>();
                        funiture.funiture_id = door_id;


                    }
                    else {
                        TraceLog(LOG_WARNING, "家具点(%f,%f)缺少door_id属性，跳过", objPos.x + 0.0f, objPos.y + 0.0f);
                        continue;
                    }

                    if (props.hasProperty("tip")) {
                        std::string tip = props.getProperty("tip")->getValue<std::string>();
                        if (tip.empty()) { TraceLog(LOG_WARNING, "没有读取到家具的tip"); }
                        funiture.tip = tip;
                        //TraceLog(LOG_INFO, "门:%s的tip是:%s", funiture.funiture_id.c_str(), funiture.tip.c_str());

                    }
                    else {
                        TraceLog(LOG_WARNING, "家具点(%f,%f)缺少door_id属性，跳过", objPos.x + 0.0f, objPos.y + 0.0f);
                        continue;
                    }

                    // 3. 加入锁住的家具列表
                    g_gameData.funitures.push_back(funiture);

                }
            }
        }
    }
	return true;
}

void RenderMap(
    const Camera2D& camera,
    const tson::Map* map,
    int mapWidth, int mapHeight,
    int tileWidth, int tileHeight,
    tson::Layer* groundLayer,
    tson::Layer* collisionLayer,
    tson::Layer* openedFunituresLayer,
    tson::Layer* funitureLayer,
    const std::unordered_map<uint32_t, TilesetData>& tilesetMap,
    bool showCollisionLayer) {
    // --- 绘制地图 Ground 图层 ---
    for (int y = 0; y < mapHeight; y++)
    {
        for (int x = 0; x < mapWidth; x++)
        {
            tson::Tile* tile = groundLayer->getTileData(x, y);
            if (tile == nullptr) continue;
            // 1. 获取瓦片所属的瓦片集）
            tson::Tileset* tileTs = tile->getTileset();
            if (!tileTs) continue;
            // 2. 从映射表中找到该瓦片集的信息
            uint32_t firstGid = tileTs->getFirstgid();
            auto it = tilesetMap.find(firstGid);
            if (it == tilesetMap.end()) {
                TraceLog(LOG_WARNING, "瓦片(%d,%d)所属的瓦片集firstGid=%d未加载", x, y, firstGid);
                continue;
            }
            const TilesetData& tsData = it->second; // 移到判断外面！

            // 解析瓦片GID及变换标志位
             // 翻转标志检测（适配你的tson版本）
            bool flipX = tile->hasFlipFlags(tson::TileFlipFlags::Horizontally);
            bool flipY = tile->hasFlipFlags(tson::TileFlipFlags::Vertically);
            bool rotate90 = tile->hasFlipFlags(tson::TileFlipFlags::Diagonally);

            // 干净的gid
            uint32_t gid = tile->getGid();
            int localId = gid - firstGid;
            if (localId < 0) {
                TraceLog(LOG_WARNING, "瓦片(%d,%d)localId=%d无效", x, y, localId);
                continue;
            }

            int baseSrcX = (localId % tsData.columns) * tileWidth;
            int baseSrcY = (localId / tsData.columns) * tileHeight;

            // 调试日志
            //TraceLog(LOG_INFO, "瓦片(%d,%d) | gid=%u | flipX=%d | flipY=%d | rotate90=%d | localId=%d | baseSrc(%d,%d)",
               // x, y, gid, flipX, flipY, rotate90, localId, baseSrcX, baseSrcY);

            float rotation = 0.0f;

            float srcX = baseSrcX;
            float srcY = baseSrcY;
            float srcWidth = tileWidth;
            float srcHeight = tileHeight;

            bool useCenterOrigin = false;  // 是否使用中心锚点

            if (rotate90) {
                // 90°/270° 对角翻转（Tiled标准：先transpose，然后post-flip）
                // 基础旋转方向：根据原flipX决定90°或270°
                rotation = flipX ? 270.0f : 90.0f;
                useCenterOrigin = true;

                // source使用正宽高 + 原始起点
                srcWidth = tileWidth;
                srcHeight = tileHeight;

                // 原vertical flip映射为旋转后的horizontal flip（负宽 + 偏移x）
                if (flipY) {
                    //srcX += tileWidth;
                    //srcWidth = -tileWidth;
                }
                // 原horizontal flip已经用于决定旋转方向，不再额外vertical flip（避免双负导致问题）
            }
            else {
                // 非对角：180°或纯翻转
                if (flipX && flipY) {
                    // 180°：正宽高 + 180旋转 + 中心锚点（最稳定）
                    rotation = 180.0f;
                    useCenterOrigin = true;
                }
                else {
                    // 纯单翻转：负宽高 + 偏移起点 + 左上锚点
                    //if (flipX) { srcX += tileWidth;  srcWidth = -tileWidth; }
                    //if (flipY) { srcY += tileHeight; srcHeight = -tileHeight; }
                    useCenterOrigin = false;
                }
            }

            Rectangle source = { srcX, srcY, srcWidth, srcHeight };

            // 目标矩形和锚点：有真实旋转（包括180°）时用中心，避免任何偏移
            Rectangle dest;
            Vector2 origin;
            if (useCenterOrigin) {
                dest = { (float)(x * tileWidth + tileWidth / 2),
                         (float)(y * tileHeight + tileHeight / 2),
                         (float)tileWidth, (float)tileHeight };
                origin = { tileWidth / 2.0f, tileHeight / 2.0f };
            }
            else {
                dest = { (float)(x * tileWidth), (float)(y * tileHeight),
                         (float)tileWidth, (float)tileHeight };
                origin = { 0.0f, 0.0f };
            }

            // TraceLog(LOG_INFO, "瓦片(%d,%d) 绘制 | source(%f,%f,%f,%f) | dest(%f,%f,%f,%f) | rotation=%.0f | useCenter=%d",
                // x, y, source.x, source.y, source.width, source.height,
                // dest.x, dest.y, dest.width, dest.height, rotation, useCenterOrigin);

            DrawTexturePro(tsData.texture, source, dest, origin, rotation, WHITE);
        }
    }

    if (funitureLayer != nullptr) {
        //========================Funiture层========================
        for (int y = 0; y < mapHeight; y++)
        {
            for (int x = 0; x < mapWidth; x++)
            {
                tson::Tile* tile = funitureLayer->getTileData(x, y);
                if (tile == nullptr) continue;

                tson::Tileset* tileTs = tile->getTileset();
                if (!tileTs) continue;

                uint32_t firstGid = tileTs->getFirstgid();
                auto it = tilesetMap.find(firstGid);
                if (it == tilesetMap.end()) {
                    TraceLog(LOG_WARNING, "瓦片(%d,%d)所属的瓦片集firstGid=%d未加载", x, y, firstGid);
                    continue;
                }
                const TilesetData& tsData = it->second;

                // 翻转标志检测
                bool flipX = tile->hasFlipFlags(tson::TileFlipFlags::Horizontally);
                bool flipY = tile->hasFlipFlags(tson::TileFlipFlags::Vertically);
                bool rotate90 = tile->hasFlipFlags(tson::TileFlipFlags::Diagonally);

                // 干净的gid
                uint32_t gid = tile->getGid();
                int localId = gid - firstGid;
                if (localId < 0) {
                    TraceLog(LOG_WARNING, "瓦片(%d,%d)localId=%d无效", x, y, localId);
                    continue;
                }

                int baseSrcX = (localId % tsData.columns) * tileWidth;
                int baseSrcY = (localId / tsData.columns) * tileHeight;

                // 调试日志
                //TraceLog(LOG_INFO, "瓦片(%d,%d) | gid=%u | flipX=%d | flipY=%d | rotate90=%d | localId=%d | baseSrc(%d,%d)",
                   // x, y, gid, flipX, flipY, rotate90, localId, baseSrcX, baseSrcY);

                float rotation = 0.0f;

                float srcX = baseSrcX;
                float srcY = baseSrcY;
                float srcWidth = tileWidth;
                float srcHeight = tileHeight;

                bool useCenterOrigin = false;  // 是否使用中心锚点

                if (rotate90) {
                    // 90°/270° 对角翻转
                    rotation = flipX ? 270.0f : 90.0f;
                    useCenterOrigin = true;

                    // 正宽高 + 额外翻转调整（对角翻转的标准处理）
                    bool diagFlipX = flipY;
                    bool diagFlipY = !flipX;

                    // if (diagFlipX) { srcX += tileWidth;  srcWidth = -tileWidth; }
                     //if (diagFlipY) { srcY += tileHeight; srcHeight = -tileHeight; }
                }
                else {
                    // 普通情况：优先用 rotation=180 处理双翻转（更稳定，避免负宽高边缘问题）
                    if (flipX && flipY) {
                        rotation = 180.0f;
                        useCenterOrigin = true;
                        // source 保持正宽高、原始起点
                        srcWidth = tileWidth;
                        srcHeight = tileHeight;
                    }
                    else {
                        // 纯水平或纯垂直翻转：用负宽高 + 起点偏移
                       // if (flipX) { srcX += tileWidth;  srcWidth = -tileWidth; }
                        //if (flipY) { srcY += tileHeight; srcHeight = -tileHeight; }
                        useCenterOrigin = false;
                    }
                }

                Rectangle source = { srcX, srcY, srcWidth, srcHeight };

                // 目标矩形和锚点：有真实旋转（包括180°）时用中心，避免任何偏移
                Rectangle dest;
                Vector2 origin;
                if (useCenterOrigin) {
                    dest = { (float)(x * tileWidth + tileWidth / 2),
                             (float)(y * tileHeight + tileHeight / 2),
                             (float)tileWidth, (float)tileHeight };
                    origin = { tileWidth / 2.0f, tileHeight / 2.0f };
                }
                else {
                    dest = { (float)(x * tileWidth), (float)(y * tileHeight),
                             (float)tileWidth, (float)tileHeight };
                    origin = { 0.0f, 0.0f };
                }

                // TraceLog(LOG_INFO, "瓦片(%d,%d) 绘制 | source(%f,%f,%f,%f) | dest(%f,%f,%f,%f) | rotation=%.0f | useCenter=%d",
                    // x, y, source.x, source.y, source.width, source.height,
                    // dest.x, dest.y, dest.width, dest.height, rotation, useCenterOrigin);

                DrawTexturePro(tsData.texture, source, dest, origin, rotation, WHITE);
            }
        }

    }





    // ==================== 绘制Collision图层（） ====================

    if (showCollisionLayer && collisionLayer != nullptr) {
        for (int y = 0; y < mapHeight; y++)
        {
            for (int x = 0; x < mapWidth; x++)
            {
                tson::Tile* tile = collisionLayer->getTileData(x, y);
                if (tile == nullptr) continue;
                // 1. 获取瓦片所属的瓦片集
                tson::Tileset* tileTs = tile->getTileset();
                if (!tileTs) continue;
                // 2. 从映射表中找到该瓦片集的信息
                uint32_t firstGid = tileTs->getFirstgid();
                auto it = tilesetMap.find(firstGid);
                if (it == tilesetMap.end()) {
                    TraceLog(LOG_WARNING, "碰撞瓦片(%d,%d)所属的瓦片集firstGid=%d未加载", x, y, firstGid);
                    continue;
                }
                const TilesetData& tsData = it->second;
                // 解析瓦片GID及变换标志位
                bool origFlipX = tile->hasFlipFlags(tson::TileFlipFlags::Horizontally);
                bool origFlipY = tile->hasFlipFlags(tson::TileFlipFlags::Vertically);
                bool rotate90 = tile->hasFlipFlags(tson::TileFlipFlags::Diagonally);

                uint32_t gid = tile->getGid();
                int localId = gid - firstGid;
                if (localId < 0) {
                    TraceLog(LOG_WARNING, "瓦片(%d,%d)localId=%d无效", x, y, localId);
                    continue;
                }

                int baseSrcX = (localId % tsData.columns) * tileWidth;
                int baseSrcY = (localId / tsData.columns) * tileHeight;

                // TraceLog(LOG_INFO, "瓦片(%d,%d) | gid=%u | origFlipX=%d | origFlipY=%d | rotate90=%d | localId=%d | baseSrc(%d,%d)",
                    // x, y, gid, origFlipX, origFlipY, rotate90, localId, baseSrcX, baseSrcY);

                float rotation = 0.0f;
                bool useCenterOrigin = false;

                float srcX = baseSrcX;
                float srcY = baseSrcY;
                float srcWidth = tileWidth;
                float srcHeight = tileHeight;

                bool effectiveFlipX = origFlipX;
                bool effectiveFlipY = origFlipY;

                if (rotate90) {
                    // 标准对角翻转处理（广泛用于square tiles，避免坐标交换导致远偏移）
                    rotation = origFlipX ? 270.0f : 90.0f;
                    useCenterOrigin = true;

                    // 交换并反转一个翻转标志
                    effectiveFlipX = origFlipY;
                    effectiveFlipY = !origFlipX;
                }
                else if (origFlipX && origFlipY) {
                    // 180°：正宽高 + 180旋转 + 中心锚点
                    rotation = 180.0f;
                    useCenterOrigin = true;
                    effectiveFlipX = false;
                    effectiveFlipY = false;  // 不需要额外翻转
                }
                // else: 纯翻转保持原标志
                /*
                // 统一应用有效翻转：负宽高 + 起点偏移
                if (effectiveFlipX) {
                    srcX += tileWidth;
                    srcWidth = -tileWidth;
                }
                if (effectiveFlipY) {
                    srcY += tileHeight;
                    srcHeight = -tileHeight;
                }
                */
                Rectangle source = { srcX, srcY, srcWidth, srcHeight };

                Rectangle dest;
                Vector2 origin;
                if (useCenterOrigin) {
                    dest = { (float)(x * tileWidth + tileWidth / 2),
                             (float)(y * tileHeight + tileHeight / 2),
                             (float)tileWidth, (float)tileHeight };
                    origin = { tileWidth / 2.0f, tileHeight / 2.0f };
                }
                else {
                    dest = { (float)(x * tileWidth), (float)(y * tileHeight),
                             (float)tileWidth, (float)tileHeight };
                    origin = { 0.0f, 0.0f };
                }

                //TraceLog(LOG_INFO, "瓦片(%d,%d) 绘制 | source(%f,%f,%f,%f) | dest(%f,%f,%f,%f) | rotation=%.0f | useCenter=%d",
                   // x, y, source.x, source.y, source.width, source.height,
                    //dest.x, dest.y, dest.width, dest.height, rotation, useCenterOrigin);

                DrawTexturePro(tsData.texture, source, dest, origin, rotation, WHITE);

            }
        }
    }

    //绘制openedFunituresLayer=============
    if (openedFunituresLayer != nullptr) {
        for (int y = 0; y < mapHeight; y++)
        {
            for (int x = 0; x < mapWidth; x++)
            {
                tson::Tile* tile = openedFunituresLayer->getTileData(x, y);
                if (tile == nullptr) continue;
                // 1. 获取瓦片所属的瓦片集）
                tson::Tileset* tileTs = tile->getTileset();
                if (!tileTs) continue;
                // 2. 从映射表中找到该瓦片集的信息
                uint32_t firstGid = tileTs->getFirstgid();
                auto it = tilesetMap.find(firstGid);
                if (it == tilesetMap.end()) {
                    TraceLog(LOG_WARNING, "瓦片(%d,%d)所属的瓦片集firstGid=%d未加载", x, y, firstGid);
                    continue;
                }
                const TilesetData& tsData = it->second; // 移到判断外面！

                // 解析瓦片GID及变换标志位
                 // 翻转标志检测（适配你的tson版本）
                bool flipX = tile->hasFlipFlags(tson::TileFlipFlags::Horizontally);
                bool flipY = tile->hasFlipFlags(tson::TileFlipFlags::Vertically);
                bool rotate90 = tile->hasFlipFlags(tson::TileFlipFlags::Diagonally);

                // 干净的gid
                uint32_t gid = tile->getGid();
                int localId = gid - firstGid;
                if (localId < 0) {
                    TraceLog(LOG_WARNING, "瓦片(%d,%d)localId=%d无效", x, y, localId);
                    continue;
                }

                int baseSrcX = (localId % tsData.columns) * tileWidth;
                int baseSrcY = (localId / tsData.columns) * tileHeight;

                // 调试日志
                //TraceLog(LOG_INFO, "瓦片(%d,%d) | gid=%u | flipX=%d | flipY=%d | rotate90=%d | localId=%d | baseSrc(%d,%d)",
                   // x, y, gid, flipX, flipY, rotate90, localId, baseSrcX, baseSrcY);

                float rotation = 0.0f;

                float srcX = baseSrcX;
                float srcY = baseSrcY;
                float srcWidth = tileWidth;
                float srcHeight = tileHeight;

                bool useCenterOrigin = false;  // 是否使用中心锚点

                if (rotate90) {
                    // 90°/270° 对角翻转
                    rotation = flipX ? 270.0f : 90.0f;
                    useCenterOrigin = true;

                    // 正宽高 + 额外翻转调整（对角翻转的标准处理）
                    bool diagFlipX = flipY;
                    bool diagFlipY = !flipX;

                    // if (diagFlipX) { srcX += tileWidth;  srcWidth = -tileWidth; }
                     //if (diagFlipY) { srcY += tileHeight; srcHeight = -tileHeight; }
                }
                else {
                    // 普通情况：优先用 rotation=180 处理双翻转（更稳定，避免负宽高边缘问题）
                    if (flipX && flipY) {
                        rotation = 180.0f;
                        useCenterOrigin = true;
                        // source 保持正宽高、原始起点
                        srcWidth = tileWidth;
                        srcHeight = tileHeight;
                    }
                    else {
                        // 纯水平或纯垂直翻转：用负宽高 + 起点偏移
                        if (flipX) { srcX += tileWidth;  srcWidth = -tileWidth; }
                        if (flipY) { srcY += tileHeight; srcHeight = -tileHeight; }
                        useCenterOrigin = false;
                    }
                }

                Rectangle source = { srcX, srcY, srcWidth, srcHeight };

                // 目标矩形和锚点：有真实旋转（包括180°）时用中心，避免任何偏移
                Rectangle dest;
                Vector2 origin;
                if (useCenterOrigin) {
                    dest = { (float)(x * tileWidth + tileWidth / 2),
                             (float)(y * tileHeight + tileHeight / 2),
                             (float)tileWidth, (float)tileHeight };
                    origin = { tileWidth / 2.0f, tileHeight / 2.0f };
                }
                else {
                    dest = { (float)(x * tileWidth), (float)(y * tileHeight),
                             (float)tileWidth, (float)tileHeight };
                    origin = { 0.0f, 0.0f };
                }

                // TraceLog(LOG_INFO, "瓦片(%d,%d) 绘制 | source(%f,%f,%f,%f) | dest(%f,%f,%f,%f) | rotation=%.0f | useCenter=%d",
                    // x, y, source.x, source.y, source.width, source.height,
                    // dest.x, dest.y, dest.width, dest.height, rotation, useCenterOrigin);

                for (auto funiture : g_gameData.funitures) {
                    if (CheckCollisionRecs(funiture.area, dest) && funiture.isOpened) {
                        DrawTexturePro(tsData.texture, source, dest, origin, rotation, WHITE);
                    }
                }
            }
        }
    }

}
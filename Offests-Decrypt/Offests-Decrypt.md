uint64_t decrypt::decrypt_ulong_key(uint64_t a1) //aka cl_active_item
{
    unsigned int* v1;
    int v2;
    unsigned int v3;
    unsigned int v4;
    uint64_t v6;
 
    v1 = (unsigned int*)&v6;
    v2 = 2;
    v6 = a1;
    do
    {
        v3 = (*v1 ^ 0xA0BD1F8C) + 0xE5C02668 ^ 0xD04458BA;
        v4 = (v3 >> 4) | (v3 << 28);
        *v1 = v4;
        ++v1;
        --v2;
    } while (v2);
 
    return v6;
}
Code:
namespace offsets {
	inline int static_padding = 0xB8;
	inline uint64_t Il2CppGetHandle = 0xd985dd0;
 
	namespace GameManager
	{
		inline uint64_t Class = 224963568; //bottom of GameManager search for Poolable
		inline int instance = 0x40;
		inline int prefab_pool_collection = 0x20;
		namespace PrefabPoolCollection
		{
			inline int Dictionary_uint_PrefabPool = 0x18;
			namespace DictionaryUintPrefabPool
			{
				inline int content = 0x18;
				namespace Content
				{
					inline int count = 0x18;
					namespace PrefabPool
					{
						inline int stack = 0x30;
						namespace Stack
						{
							inline int array = 0x10; //start at 0x20
							inline int count = 0x18;
							namespace Poolable
							{
								inline int behaviours = 0x28; //array , start at 0x20
								namespace Behaviours
								{
									inline int projectile = 0x28;
									namespace Projectile
									{
										inline int thiccness = 0x3C; //	public float thickness;
										inline int drag = 0x34; //	public float drag;
										inline int gravityModifier = 0x38; //	public float gravityModifier;
										inline int initialVelocity = 0x28;//	public Vector3 initialVelocity;
									}
								}
							}
						}
					}
				}
			}
		}
	}
	namespace BaseNetworkable {
		inline uint64_t Class = 0xd65a0a8; //bottom of BaseNetworkable
		inline int instance = 0x8; //public static %...<BaseNetworkable.%...> %...;
		inline int object_dictionary = 0x20; //not in dump, search with ReClass
		namespace entity_list {
			inline int content = 0x10;
			inline int size = 0x18;
		};
		inline int prefabID = 0x30; //public uint prefabID
		inline int children = 0x40; //public readonly List<global::BaseEntity> children;
		inline int parent_entity = 0x70;
	};
	namespace MainCamera {
		inline uint64_t Class = 0xd676960;	/*Script.json "Name": "MainCamera_TypeInfo","Signature": "MainCamera_c*"*/
		inline int instanse = 0x58; //public static Camera mainCamera
		inline int CameraGameObject = 0x10;
		inline int ViewMatrix = 0x30c;
		inline int position = 0x454;
	};
	namespace BaseCombatEntity {
		inline int lifestate = 0x258; //public BaseCombatEntity.LifeState lifestate;
	};
	namespace BaseEntity {
		inline int model = 0xE8; //public Model model;
		namespace Model {
			inline int boneTransforms = 0x50;//public Transform[] boneTransforms;
		};
	};
	namespace BasePlayer {
 
		//inline int player_eyes = 0X5E8; //	private global::BasePlayer.HiddenValue<PlayerEyes> eyesValue;
		namespace PlayerEyes
		{
			inline int BodyRotation = 0x50; //private Quaternion <bodyRotation>k__BackingField;
		}
		inline int player_input = 0x2F0;//public PlayerInput input;
		namespace PlayerInput
		{
			inline constexpr int ViewAngles = 0x44; //	private Vector3 bodyAnglesOverride;
		}
		inline int player_model = 0x5F0; //public PlayerModel %...;
		namespace PlayerModel {
			inline int position = 0x1f8; //internal Vector3 position;
			inline int box_colider = 0x20;
			inline int NewVelocity = 0x21c; //	private Vector3 newVelocity;
			namespace BoxColider {
				inline int internal = 0x10;
				namespace BoxColiderInternal {
					inline int center = 0x80; //Vector3
					inline int size = 0x8C; //Vector3
				}
			}
			inline int skinned_multi_mesh = 0x378; //private SkinnedMultiMesh %....;
			namespace SkinnedMultiMesh
			{
				inline int renderer_list = 0x58;//private readonly List<Renderer> %...;
 
			}
		}
		inline int playerFlags = 0x5E8;  //public BasePlayer.PlayerFlags playerFlags;
		inline int _displayName = 0x620; //protected string _displayName;
		inline int clactiveitem = 0x4d0; //private BasePlayer.EncryptedValue<ItemId> clActiveItem;
		inline int currentTeam = 0x4A0; //currentTeam
		//inline int inventory = 0X428; //private global::BasePlayer.HiddenValue<global::PlayerInventory> inventoryValue;
		namespace PlayerInventory {
			inline int belt = 0x78; //public ItemContainer containerBelt;
 
			namespace ItemContainer {
				inline int itemlist = 0x58; //public List<Item> itemList;
			};
		};
	};
	namespace WorldItem
	{
		inline int item = 0x1c8; // public global::Item item;
	};
	namespace Item
	{
		inline int item_definition = 0xD0;
		inline int item_uid = 0x60; //public ItemId uid;
	};
	namespace ItemDefinition {
		inline int shortname = 0x28; //	public string shortname;
		inline int category = 0x58; 	//public ItemCategory category;
	};
	namespace BaseViewModel {
		inline uint64_t Class = 0xD6B9878;//bottom of BaseViewModel
		inline int instanse = 0xd8; //public static List<BaseViewModel> %...;
		inline int animation_events = 0xD0; //internal AnimationEvents %...;
		namespace AnimationEvents {
			inline int targetEntity = 0x28; //public HeldEntity targetEntity; -> BaseProjectile
		};
	};
	//TOD_Sky
	namespace TodSky
	{
		inline constexpr uint64_t Class = 0xD6C6278;
		inline constexpr int Instance = 0x90;
		//inline constexpr int Ambient = 0x98;
		//namespace TOD_AmbientParameters
		//{
		//	inline constexpr int UpdateInterval = 0x18; //float
		//}
		inline constexpr int Night = 0x60;
		namespace TOD_NightParameters
		{
			inline constexpr int LightIntensity = 0x50; //float
			inline constexpr int ReflectionMultiplier = 0x64; //float
		}
		inline constexpr int Cycle = 0x40;
		namespace TOD_CycleParameters
		{
			inline constexpr int Hour = 0x10; //float
		}
	}
}


Il2CppGetHandle = 0xd985dd0
BaseNetworkable = 0xd65a0a8
MainCamera = 0xd676960
Graphics = 0xd68f150
ListComponent_Projectile__c = 0xD6F1350
PlayerEyes_c = 0xD72A6E8
 
List::items = 0x10
List::size = 0x18
List::first_element = 0x20
 
ListComponent::internal_list = 0x18
 
Projectile::drag = 0x34
Projectile::gravityModifier = 0x38
Projectile::thickness = 0x3C
Projectile::owner = 0x1E0
Projectile::itemModProjectile = 0x110
 
ItemModProjectile::projectileSpread = 0x3C
ItemModProjectile::projectileVelocity = 0x40
 
Eoka::successFraction = 0x438
 
Chains::Camera = 0x80
Chains::ClientEntities = 0x8
Chains::EntityList = 0x10
Chains::BufferList = 0x20
Chains::BaseNetworkable_Static = 0xB8
Chains::BaseNetworkable_1 = 0x8
Chains::BaseNetworkable_2 = 0x10
Chains::BaseNetworkable_3 = 0x20
Chains::Camera_Static = 0xB8
Chains::Camera_1 = 0x58
Chains::Camera_2 = 0x10
Chains::TodSky_Instance = 0x60
Chains::GameManager_1 = 0x40
 
// Camera
Camera::Base = 0xd676960
Camera::StaticOffset = 0xB8
Camera::ObjectOffset = 0x58
Camera::Offset = 0x10
Camera::Matrix = 0x30C
 
// ConvarGraphics
ConvarGraphics::Base = 0xd68f150
ConvarGraphics::StaticFields = 0xB8
ConvarGraphics::Fov = 0x2a0
 
// GameManager
GameManager::PrefabPoolCollection = 0x18
GameManager::StorageDictionary = 0x18
GameManager::StorageStackPoolable = 0x18
 
// TodSky
TodSky::Instance = 0x60
TodSky::NightParameters = 0x60
TodSky::DayParameters = 0x58
TodSky::AmbientParameters = 0x98
 
// TodNightParameters
TodNightParameters::AmbientMultiplier = 0x5C
TodNightParameters::LightIntensity = 0x50
 
// TodDayParameters
TodDayParameters::AmbientMultiplier = 0x54
 
// TodAmbientParameters
TodAmbientParameters::UpdateInterval = 0x18
 
// BasePlayer
BasePlayer::clActiveItem = 0x4D0
BasePlayer::playerModel = 0x5F0
BasePlayer::playerEyes = 0x5F8
BasePlayer::playerInventory = 0x658
BasePlayer::currentTeam = 0x4A0
BasePlayer::playerInput = 0x2F0
BasePlayer::playerFlags = 0x5E8
BasePlayer::_displayName = 0x620
BasePlayer::modelState = 0x2c0
BasePlayer::baseMovement = 0x508
BasePlayer::clothingMoveSpeedReduction = 0x6c8
 
// BaseNetworkableClass
BaseNetworkableClass::static_fields = 0xB8
BaseNetworkableClass::wrapper_class_ptr = 0x8
BaseNetworkableClass::parent_static_fields = 0x10
BaseNetworkableClass::entity = 0x10
BaseNetworkableClass::List = 0x10
BaseNetworkableClass::Count = 0x18
BaseNetworkableClass::prefabID = 0x30
BaseNetworkableClass::class_name = 0x10
 
Model::collision = 0x20
Model::rootBone = 0x28
Model::headBone = 0x30
Model::eyeBone = 0x38
Model::animator = 0x40
Model::skeleton = 0x48
Model::boneTransforms = 0x50
Model::boneNames = 0x58
 
ModelState::flags = 0x24
 
BaseEntity::bounds = 0xBC
BaseEntity::model = 0xE8
BaseEntity::SkinnedMultiMesh = 0x168
 
SkinnedMultiMesh::SkinnedRenderer = 0x58
 
BaseCombatEntity::_health = 0x264
BaseCombatEntity::_maxHealth = 0x268
BaseCombatEntity::lifestate = 0x258
 
PlayerModel::position = 0x1f8
PlayerModel::newVelocity = 0x21C
PlayerModel::skinnedMultiMesh = 0x2A8
PlayerModel::visible = 0x191
PlayerModel::isLocalPlayer = 0x279
PlayerModel::renderersList = 0x50
 
// WorldItem
WorldItem::item = 0x1C8
 
// PlayerInventory
PlayerInventory::containerBelt = 0x78
PlayerInventory::containerMain = 0x30
PlayerInventory::containerWear = 0x58
 
ItemContainer::itemList = 0x58
 
ItemDefinition::itemid = 0x20
ItemDefinition::shortname = 0x28
ItemDefinition::displayName = 0x40
ItemDefinition::category = 0x58
 
TranslatePhrase::english = 0x20
 
Item::uid = 0x60
Item::amount = 0x78
Item::heldEntity = 0x90
Item::ItemDefinition = 0xD0
 
BaseProjectile::NoiseRadius = 0x300
BaseProjectile::damageScale = 0x304
BaseProjectile::distanceScale = 0x308
BaseProjectile::projectileVelocityScale = 0x30C
BaseProjectile::automatic = 0x310
BaseProjectile::usableByTurret = 0x311
BaseProjectile::turretDamageScale = 0x314
BaseProjectile::reloadTime = 0x350
BaseProjectile::canUnloadAmmo = 0x354
BaseProjectile::primaryMagazine = 0x358
BaseProjectile::fractionalReload = 0x360
BaseProjectile::aimSway = 0x378
BaseProjectile::aimSwaySpeed = 0x37C
BaseProjectile::RecoilProperties = 0x380
BaseProjectile::aimconeCurve = 0x388
BaseProjectile::aimCone = 0x390
BaseProjectile::hipAimCone = 0x394
BaseProjectile::aimconePenaltyPerShot = 0x398
BaseProjectile::aimConePenaltyMax = 0x39C
BaseProjectile::stancePenaltyScale = 0x3A8
BaseProjectile::hasADS = 0x3AC
BaseProjectile::noAimingWhileCycling = 0x3AD
BaseProjectile::manualCycle = 0x3AE
BaseProjectile::aiming = 0x3B6
BaseProjectile::isBurstWeapon = 0x3B7
BaseProjectile::internalBurstRecoilScale = 0x3BC
BaseProjectile::internalBurstFireRateScale = 0x3C0
BaseProjectile::internalBurstAimConeScale = 0x3C4
BaseProjectile::numShotsFired = 0x3CC
BaseProjectile::sightAimConeScale = 0x3EC
BaseProjectile::hipAimConeScale = 0x3F0
BaseProjectile::viewModel = 0x250
 
ViewModel::instance = 0x28
 
BaseViewModel::useViewModelCamera = 0x40
BaseViewModel::model = 0x80
BaseViewModel::ironSights = 0xC0
BaseViewModel::lower = 0xA0
BaseViewModel::ViewmodelBob = 0xC8
BaseViewModel::ViewmodelSway = 0xA8
BaseViewModel::ViewmodelPunch = 0xE8
 
IronSights::zoomFactor = 0x2C
IronSights::ironSightsOverride = 0x68
 
RecoilProperties::recoilYawMin = 0x18
RecoilProperties::recoilYawMax = 0x1C
RecoilProperties::recoilPitchMin = 0x20
RecoilProperties::recoilPitchMax = 0x24
RecoilProperties::aimconeCurveScale = 0x60
RecoilProperties::newRecoilOverride = 0x80
 
BowWeapon::attackReady = 0x438
BowWeapon::arrowBack = 0x43C
BowWeapon::swapArrows = 0x440
BowWeapon::wasAiming = 0x448
 
Magazine::definition = 0x10
Magazine::capacity = 0x18
Magazine::contents = 0x1C
Magazine::ammoType = 0x20
 
HeldEntity::ownerItemUID = 0x220
HeldEntity::viewModel = 0x250
 
ViewmodelBob::bobSpeedWalk = 0x20
ViewmodelBob::bobSpeedRun = 0x24
ViewmodelBob::bobAmountWalk = 0x28
ViewmodelBob::bobAmountRun = 0x2C
ViewmodelBob::leftOffsetRun = 0x30
 
 
ViewmodelPunch::punchAmount = 0x34
ViewmodelPunch::punchRecovery = 0x38
 
ViewmodelLower::lowerOnSprint = 0x20
ViewmodelLower::lowerWhenCantAttack = 0x21
ViewmodelLower::shouldLower = 0x28
ViewmodelLower::rotateAngle = 0x2C





inline uintptr_t decrypt_PlayerInventory(uintptr_t epstein)
{
	if (!epstein) return 0;
 
	std::uint32_t r8d = 0, eax = 0, ecx = 0, edx = 0;
	std::uintptr_t rax = driver->Read<std::uintptr_t>(epstein+ 0x18);
	if (!rax) return 0;
 
	std::uintptr_t* rdx = &rax;
	r8d = 0x02;
	do {
		ecx = *(std::uint32_t*)(rdx);
		rdx = (std::uintptr_t*)((char*)rdx + 0x04);
		ecx -= 0x600B999C;
		ecx ^= 0xE017EC85;
		eax = ecx;
		ecx <<= 0x06;
		eax >>= 0x1A;
		eax |= ecx;
		*((std::uint32_t*)rdx - 1) = eax;
		r8d -= 0x01;
	} while (r8d);
 
	return Il2cppGetHandle(static_cast<int32_t>(rax));
}
 
inline uintptr_t decrypt_PlayerEyes(uintptr_t a1)
{
	if (!a1) return 0;
 
	std::uint32_t r8d = 0, eax = 0, ecx = 0, edx = 0;
	std::uintptr_t rax = driver->Read<std::uintptr_t>(a1 + 0x18);
	if (!rax) return 0;
 
	std::uintptr_t* rdx = &rax;
	r8d = 0x02;
	do {
		eax = *(std::uint32_t*)(rdx);
		rdx = (std::uintptr_t*)((char*)rdx + 0x04);
		eax += 0x5A59459F;
		ecx = eax;
		eax += eax;
		ecx >>= 0x1F;
		ecx |= eax;
		ecx += 0x533DF48A;
		eax = ecx;
		ecx <<= 0x05;
		eax >>= 0x1B;
		eax |= ecx;
		*((std::uint32_t*)rdx - 1) = eax;
		r8d -= 0x01;
	} while (r8d);
	return Il2cppGetHandle(static_cast<int32_t>(rax));
}
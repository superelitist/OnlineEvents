
#define NOMINMAX

#include "..\..\inc\INIReader.h"
#include "..\..\inc\INIWriter.h"
#include "..\..\inc\keyboard.h"

#include "data.h"
#include "Log.h"
#include "script.h"
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <numeric>
#include <random>
#include <set>
#include <sstream>
//#include "iostream"
//#include "strings.h"

//#include <cmath>
//#include <ctime>
//#include <ratio>
//#include <string> // included in Log.h
#include <vector> // lol why don't I need to include this?



CIniReader Reader(".\\OnlineEventsRedux.ini");
Log Logger(".\\OnlineEventsRedux.log", LogNormal);
std::ofstream xyz_file;
std::ofstream crate_spawn_file;

#pragma warning(disable : 4244 4305) // double <-> float conversions
#pragma warning(disable : 4302)

std::random_device random_device; std::mt19937 generator(random_device()); // init a standard mersenne_twister_engine
uint times_waited = 0;

Player player;
Ped player_ped;
Vector4 player_position; // these get set every update
std::vector<Vector4> reserved_vehicle_spawn_points;
std::vector<Vector4> vehicle_spawn_points;
std::vector<Vector4> special_marker_points;
std::vector<Hash> possible_vehicle_models;
std::set<Blip> crate_spawn_blips;
bool crate_spawn_blips_current_state = false;
Vector4 current_mission_objective;

// preference options
bool load_without_notification, play_notification_beeps, use_default_blip;
uint mission_cooldown, mission_timeout;
uint spawn_point_minimum_range, spawn_point_maximum_range;
uint mission_minimum_range_for_timeout;
float mission_reward_modifier;
int stealable_vehicle_classes;
int destroyable_vehicle_classes;
uint number_of_guards_to_spawn;
uint time_between_guard_respawns;

// debug options
LogLevel logging_level;
bool debug_enable;
uint seconds_to_wait_for_vehicle_persistence_scripts, vehicle_search_range_minimum;
uint maximum_number_of_spawn_points, maximum_number_of_vehicle_models, distance_to_draw_debug_markers;
bool dump_parked_cars_to_xyz_file;

inline void Wait(uint milliseconds) {
	times_waited += 1;
	WAIT(milliseconds);
}

inline bool FileExists(const std::string& name) {
	struct stat buffer;
	return (stat(name.c_str(), &buffer) == 0);
}

inline bool IsControlPressed(eControl control) {
	return CONTROLS::IS_DISABLED_CONTROL_PRESSED(2, control);
}

inline bool IsControlJustReleased(eControl control) {
	return CONTROLS::IS_DISABLED_CONTROL_JUST_RELEASED(2, control);
}

inline void NotifyBottomCenter(char* message, unsigned duration) {
	UI::BEGIN_TEXT_COMMAND_PRINT("STRING");
	UI::ADD_TEXT_COMPONENT_SUBSTRING_PLAYER_NAME(message);
	UI::END_TEXT_COMMAND_PRINT(duration, 1);
	Logger.Write("NotifyBottomCenter(): " + std::string(message), LogVerbose);
}

inline void PlayNotificationBeep() {
	if (play_notification_beeps) AUDIO::PLAY_SOUND_FRONTEND(-1, "Text_Arrive_Tone", "Phone_SoundSet_Default", 0);
}

inline void CreateNotification(char* message, bool is_beep_enabled) {
	UI::_SET_NOTIFICATION_TEXT_ENTRY("STRING");
	UI::ADD_TEXT_COMPONENT_SUBSTRING_PLAYER_NAME(message);
	UI::_DRAW_NOTIFICATION(0, 1);
	if (is_beep_enabled) AUDIO::PLAY_SOUND_FRONTEND(-1, "Text_Arrive_Tone", "Phone_SoundSet_Default", 0);
	Logger.Write("CreateNotification(): " + std::string(message), LogNormal);
}

inline double Radians(double degrees) {
	return degrees * (M_PI / 180);
}

inline Vector4 GetVector4OfEntity(Entity entity) {
	return Vector4 { ENTITY::GET_ENTITY_COORDS(entity, 0), ENTITY::_GET_ENTITY_PHYSICS_HEADING(entity) };
}

inline Vector3 GetCoordinateByOriginBearingAndDistance(Vector4 v4, float bearing, float distance) {
	//Vector3 v3;	
	//v3.x = v4.x + cos((bearing)*radian) * distance; v3.y = v4.y + sin((bearing)*radian) * distance; v3.z = v4.z;
	Vector3 result = Vector3{ static_cast<float>(v4.x + cos((bearing)*radian) * distance), 0 , static_cast<float>(v4.y + sin((bearing)*radian) * distance), 0, static_cast<float>(v4.z), 0 };
	std::stringstream ss;
	ss << std::setprecision(6) << "GetCoordinatesByOriginBearingAndDistance( (" << v4.x << ", " << v4.y << ", " << v4.z << "), " << bearing << ", " << distance << " ): (" << result.x << ", " << result.y << ", " << result.z << " )";
	Logger.Write(ss.str(), LogDebug);
	return result;
}

inline double GetAngleBetween2DCoords(float ax, float ay, float bx, float by) {
	float x_diff = bx - ax;
	float y_diff = by - ay;
	double theta = atan2(x_diff, y_diff);
	double result = theta / radian;
	Logger.Write("GetAngleBetween2DCoords( " + std::to_string(ax) + ", " + std::to_string(ay) + ", " + std::to_string(bx) + ", " + std::to_string(by) + " ): " + std::to_string(result), LogDebug);
	return result;
}

double NormalizeToDegrees(double x) {
	x = fmod(x, 360);
	if (x < 0)
		x += 360;
	return x;
}

inline double GetDistanceBetween2DCoords(float ax, float ay, float bx, float by) {
	double result = std::hypot(bx - ax, by - ay);
	//Logger.Write("GetDistanceBetween2DCoords( " + std::to_string(ax) + ", " + std::to_string(ay) + ", " + std::to_string(bx) + ", " + std::to_string(by) + " ): " + std::to_string(result), LogVerbose);
	return result;
}

inline double GetDistanceBetween2DCoords(Vector4 a, Vector4 b) {
	double result = std::hypot(b.x - a.x, b.y - a.y);
	return result;
}

inline double GetDistanceBetween3DCoords(float ax, float ay, float az, float bx, float by, float bz) {
	return std::sqrt(std::pow(ax - bx, 2) + std::pow(ay - by, 2) + std::pow(az - bz, 2));
}

inline double GetDistanceBetween3DCoords(Vector3 vec_a, Vector3 vec_b) {
	return std::sqrt(std::pow(vec_a.x - vec_b.x, 2) + std::pow(vec_a.y - vec_b.y, 2) + std::pow(vec_a.z - vec_b.z, 2));
}

inline double GetDistanceBetween3DCoords(Vector4 vec_a, Vector4 vec_b) {
	return std::sqrt(std::pow(vec_a.x - vec_b.x, 2) + std::pow(vec_a.y - vec_b.y, 2) + std::pow(vec_a.z - vec_b.z, 2));
}

inline bool GetIsDistanceBetween3DCoordsLessThan( Vector4 vec_a, Vector4 vec_b, double distance ) {
	if ( !(std::abs( vec_a.x - vec_b.x ) < distance) ) return false;
	if ( !(std::abs( vec_a.x - vec_b.x ) < distance) ) return false;
	if ( !(std::abs( vec_a.x - vec_b.x ) < distance) ) return false;
	if ( !std::sqrt( std::pow( vec_a.x - vec_b.x, 2 )
		+ std::pow( vec_a.y - vec_b.y, 2 )
		+ std::pow( vec_a.z - vec_b.z, 2 ) < distance ) ) return false;
	return true;
}

inline float GetGroundZAtThisLocation(Vector4 v4) {
	if (v4.z > 100.0f) {
		Logger.Write("GetGroundZAtThisLocation(): v4.z is already over 1000, something went wrong!", LogError);
		return 9999;
	}
	float ground_z0;
	(GAMEPLAY::GET_GROUND_Z_FOR_3D_COORD(v4.x, v4.y, v4.z, &ground_z0, 0));
	Wait(0);
	if (ground_z0 == 0) {
		v4.z += 1.0f;
		//Wait(0);
		ground_z0 = GetGroundZAtThisLocation(v4);
	}
	//std::stringstream ss; ss << std::fixed << std::setprecision(2) << "GetGroundZAtThisLocation(" << v4.x << ", " << v4.y << ", " << v4.z << ") GroundZ(0): " << ground_z0; Logger.Write(ss.str(), LogDebug);
	return ground_z0;
}

inline double GetFromUniformRealDistribution(double first, double second) {
	std::uniform_real_distribution<> uniform_real_distribution(first, second);
	double result =  uniform_real_distribution(generator);
	//Logger.Write("GetFromUniformRealDistribution( " + std::to_string(first) + ", " + std::to_string(second) + " ): " + std::to_string(result), LogDebug);
	return result;
}

inline int GetFromUniformIntDistribution(int first, int second) {
	std::uniform_int_distribution<> uniform_int_distribution(first, second);
	int result = uniform_int_distribution(generator);
	Logger.Write("GetFromUniformIntDistribution( " + std::to_string(first) + ", " + std::to_string(second) + " ): " + std::to_string(result), LogDebug);
	return result;
}

inline void ChangeMoneyForCurrentPlayer(int value, float modifier) {
	value = int(value * modifier);
	int current_player = 99; // Code analysis claims this should be initialized. Obviously, I know that playerPed MUST resolve to 0, 1, or 2, but for the sake of technicality, I'm initializing it here. Of course, if any of those ifs ever DON'T resolve to 0, 1, or 2, I'm gonna crash anyway...
	int player_cash;
	if (PED::IS_PED_MODEL(player_ped, GAMEPLAY::GET_HASH_KEY("player_zero"))) current_player = 0;
	if (PED::IS_PED_MODEL(player_ped, GAMEPLAY::GET_HASH_KEY("player_one")))  current_player = 1;
	if (PED::IS_PED_MODEL(player_ped, GAMEPLAY::GET_HASH_KEY("player_two")))  current_player = 2;
	char statNameFull[32];
	sprintf_s(statNameFull, "SP%d_TOTAL_CASH", current_player);
	Hash hash = GAMEPLAY::GET_HASH_KEY(statNameFull);
	STATS::STAT_GET_INT(hash, &player_cash, -1);
	player_cash += value;
	STATS::STAT_SET_INT(hash, player_cash, 1);
	Logger.Write("ChangeMoneyForCurrentPlayer(): added " + std::to_string(value) + " dollars.", LogVerbose);
}

inline Hash GetHashOfVehicleModel(Vehicle vehicle) {
	return ENTITY::GET_ENTITY_MODEL(vehicle);
}

inline int GetVehicleClassBitwiseFromHash(Hash hash) {
	int raw_int = VEHICLE::GET_VEHICLE_CLASS_FROM_NAME(hash);
	Logger.Write("GetVehicleClassBitwiseFromHash(): " + std::string(VehicleClasses[raw_int]), LogDebug); // from strings.h because c++ sucks at some things.
	switch (raw_int) {
	case  0: return Compact;
	case  1: return Sedan;
	case  2: return SUV;
	case  3: return Coupe;
	case  4: return Muscle;
	case  5: return SportsClassic;
	case  6: return Sports;
	case  7: return Super;
	case  8: return Motorcycle;
	case  9: return OffRoad;
	case 10: return Industrial;
	case 11: return Utility;
	case 12: return Van;
	case 13: return Cycle;
	case 14: return Boat;
	case 15: return Helicopter;
	case 16: return Plane;
	case 17: return Service;
	case 18: return Emergency;
	case 19: return Military;
	case 20: return Commercial;
	case 21: return Trainn;
	}
	return 0;
}

inline void SetPlayerMinimumWantedLevel(WantedLevel wanted_level) {
	Logger.Write("SetPlayerMinimumWantedLevel()", LogVerbose);
	if (PLAYER::GET_PLAYER_WANTED_LEVEL(player) < wanted_level) {
		PLAYER::SET_PLAYER_WANTED_LEVEL(player, wanted_level, 0);
		PLAYER::SET_PLAYER_WANTED_LEVEL_NOW(player, 1);
	}
}

inline bool IsVehicleProbablyParked(Vehicle vehicle) {
	if ((VEHICLE::IS_VEHICLE_STOPPED(vehicle)) &&
		(VEHICLE::GET_PED_IN_VEHICLE_SEAT(vehicle, -1) == 0) && // IS_VEHICLE_SEAT_FREE doesn't fucking work.
		(VEHICLE::GET_VEHICLE_NUMBER_OF_PASSENGERS(vehicle) == 0)) {
		return true;
	}
	return false;
}

inline bool IsVehicleDrivable(Vehicle vehicle) {
	Hash hash_of_vehicle_model = GetHashOfVehicleModel(vehicle);
	if (VEHICLE::IS_VEHICLE_DRIVEABLE(vehicle, false) &&
		(VEHICLE::IS_THIS_MODEL_A_CAR(hash_of_vehicle_model) ||
		VEHICLE::IS_THIS_MODEL_A_BIKE(hash_of_vehicle_model) ||
		VEHICLE::IS_THIS_MODEL_A_QUADBIKE(hash_of_vehicle_model))) {
		return true;
	}
	return false;
}

inline bool DoesEntityExistAndIsNotNull(Entity entity) {
	return (entity != NULL && ENTITY::DOES_ENTITY_EXIST(entity));
}

inline std::vector<Hash> GetVehicleModelsFromWorld(Ped ped, std::vector<Hash> vector_of_hashes, int maximum_vector_size) {
	const int ARR_SIZE = 1024;
	Vehicle all_world_vehicles[ARR_SIZE];
	int count = worldGetAllVehicles(all_world_vehicles, ARR_SIZE);
	if (all_world_vehicles != NULL) {
		for (int i = 0; i < count; i++) {
			if (DoesEntityExistAndIsNotNull(all_world_vehicles[i])) {
				Vehicle this_vehicle = all_world_vehicles[i];
				if (DoesEntityExistAndIsNotNull(this_vehicle)) {
					Hash this_vehicle_hash = GetHashOfVehicleModel(this_vehicle);
					auto predicate = [this_vehicle_hash](const Hash & item) { // pretty sure this isn't necessary for hashes, but it works.
						return (item == this_vehicle_hash);
					};
					bool found = (std::find_if(vector_of_hashes.begin(), vector_of_hashes.end(), predicate) != vector_of_hashes.end());
					if (!found) {
						char *this_vehicle_display_name = VEHICLE::GET_DISPLAY_NAME_FROM_VEHICLE_MODEL(this_vehicle_hash);
						char *this_vehicle_string_literal = UI::_GET_LABEL_TEXT(this_vehicle_display_name);
						Logger.Write("GetVehicleModelsFromWorld(): " + std::string(this_vehicle_string_literal), LogVerbose);
						vector_of_hashes.push_back(this_vehicle_hash);
					}
					if (vector_of_hashes.size() > maximum_vector_size) vector_of_hashes.erase(vector_of_hashes.begin()); // also pretty sure there's not more than a thousand models in the game, but safety first...
				}	
			}
		}
	}
	return vector_of_hashes;
}

inline std::vector<Vector4> GetParkedVehiclesFromWorld(Ped ped, std::vector<Vector4> vector_of_vector4s, int maximum_vector_size, int search_range_minimum) {
	std::chrono::high_resolution_clock::time_point time_point_at_search_start = std::chrono::high_resolution_clock::now();
	std::chrono::high_resolution_clock::time_point time_point_at_this_loop_instance = time_point_at_search_start;
	uint times_waited = 0;
	const int ARR_SIZE = 1024;
	Vehicle all_world_vehicles[ARR_SIZE];
	int count = worldGetAllVehicles(all_world_vehicles, ARR_SIZE);
	if (all_world_vehicles != NULL) {
		for (int i = 0; i < count; i++) {
			if (DoesEntityExistAndIsNotNull(all_world_vehicles[i])) {
				Vehicle this_vehicle = all_world_vehicles[i];
				Vector4 this_vehicle_position = { GetVector4OfEntity(this_vehicle) };
				if (DoesEntityExistAndIsNotNull(this_vehicle) &&
					!GetIsDistanceBetween3DCoordsLessThan( player_position, this_vehicle_position, (double)search_range_minimum) &&
					IsVehicleDrivable(this_vehicle) && // is the vehicle a car/bike/etc and can the player start driving it?
					IsVehicleProbablyParked(this_vehicle) && // not moving, no driver?
					!(VEHICLE::GET_LAST_PED_IN_VEHICLE_SEAT(this_vehicle, -1) == player_ped) && // probably not previously used by the player? We can hope?
					!(VEHICLE::_IS_VEHICLE_DAMAGED(this_vehicle)) && // probably not an empty car in the street as a result of a pileup...
					(VEHICLE::_IS_VEHICLE_SHOP_RESPRAY_ALLOWED(this_vehicle)) // this doesn't seem to work...
					) {
					// CHECK WHETHER THERE IS A BLIP ATTACHED TO THIS VEHICLE
					std::chrono::high_resolution_clock::time_point time_point_at_search_current = std::chrono::high_resolution_clock::now();
					std::chrono::duration<double> time_span = std::chrono::duration_cast<std::chrono::duration<double>>(time_point_at_search_current - time_point_at_this_loop_instance);
					float ticks_taken_by_search_so_far = time_span.count() * 1000;
					if (ticks_taken_by_search_so_far > 15.0f) {
						std::chrono::high_resolution_clock::time_point time_point_at_this_loop_instance = std::chrono::high_resolution_clock::now();
						times_waited += 1;
						Wait(0); // The ifs should be fast, but comparing ~150 vehicles in the game world to upwards of 4000 spawn points actually takes appreciable time. Wait(0) hands control back to the game engine for a tick, making sure we don't slow the game down.
					}
					auto predicate = [this_vehicle_position](const Vector4 & item) { // didn't want to define a lambda inline, it gets ugly fast.
						bool too_close_to_another_point = ( GetIsDistanceBetween3DCoordsLessThan( this_vehicle_position, item, 1.0 ) );
						return (too_close_to_another_point);
					};
					bool found_in_vector = (std::find_if(vector_of_vector4s.begin(), vector_of_vector4s.end(), predicate) != vector_of_vector4s.end()); // make sure this_vehicle_position does not already exist in vector_of_vector4s
					if (!found_in_vector) {
						vector_of_vector4s.push_back(this_vehicle_position);
						if (dump_parked_cars_to_xyz_file) {
							Hash this_vehicle_hash = GetHashOfVehicleModel(this_vehicle);
							char *this_vehicle_display_name = VEHICLE::GET_DISPLAY_NAME_FROM_VEHICLE_MODEL(this_vehicle_hash);
							char *this_vehicle_string_literal = UI::_GET_LABEL_TEXT(this_vehicle_display_name);
							xyz_file << std::setprecision(9) << this_vehicle_string_literal << "," << this_vehicle_position.x << "," << this_vehicle_position.y << "," << this_vehicle_position.z << "," << this_vehicle_position.h << std::endl;
						}
					}
					if (vector_of_vector4s.size() > maximum_vector_size) vector_of_vector4s.erase(vector_of_vector4s.begin()); // just in case it gets filled up, first in first out
				}
			}
		}
	}
	std::chrono::high_resolution_clock::time_point time_point_at_search_end = std::chrono::high_resolution_clock::now();
	std::chrono::duration<double> time_span = std::chrono::duration_cast<std::chrono::duration<double>>(time_point_at_search_end - time_point_at_search_start);
	float ticks_taken_to_complete_search = time_span.count() * 1000;
	if (times_waited > 0) {
		Logger.Write("GetParkedCarsFromWorld(): times_waited:" + std::to_string(times_waited) + ",  time to complete: " + std::to_string(ticks_taken_to_complete_search) + ",  vehicles: " + std::to_string(count) + ",  vector size: " + std::to_string(vector_of_vector4s.size()), LogDebug);
	}
	return vector_of_vector4s;
}

/// <summary>
/// Creates a raycast between 2 points.
/// </summary>
/// <param name="source">The source of the raycast.</param>
/// <param name="target">The target of the raycast.</param>
/// <param name="options">What type of objects the raycast should intersect with.</param>
/// <param name="ignoreEntity">Specify an <see cref="Entity"/> that the raycast should ignore, leave null for no entities ignored.</param>
inline bool GetSimpleRaycast( Vector3 source, Vector3 target, int options = 1, Entity ignoreEntity = NULL) {
	
	// UNTESTED
	BOOL hitSomethingArg;	Vector3 hitPositionArg;	int entityHandleArg;	Vector3 surfaceNormalArg;
	int result = WORLDPROBE::_START_SHAPE_TEST_RAY( source.x, source.y, source.z, target.x, target.y, target.z, options, ignoreEntity == NULL ? 0 : ignoreEntity, 7 );
	Wait( 0 );
	WORLDPROBE::GET_SHAPE_TEST_RESULT( result, &hitSomethingArg, &hitPositionArg, &surfaceNormalArg, &entityHandleArg );
	return hitSomethingArg;
}

inline bool IsSpawnUnseenByPlayer( Vector3 location, Hash hash ) {
	return !ENTITY::WOULD_ENTITY_BE_OCCLUDED( hash, location.x, location.y, location.z, true );
}

inline bool IsSpawnUnseenByPlayer( Vector4 location, Hash hash ) {
	return !ENTITY::WOULD_ENTITY_BE_OCCLUDED( hash, location.x, location.y, location.z, true );
}

inline bool IsPointBehindCamera( Vector3 point ) {
	Vector3 coord = CAM::GET_GAMEPLAY_CAM_COORD();
	float fov = CAM::GET_GAMEPLAY_CAM_FOV();
	float half_fov = fov / 2;
	float angle = GetAngleBetween2DCoords(coord.x, coord.y, point.x, point.y);
	angle = angle - ENTITY::GET_ENTITY_HEADING( player_ped );
	return std::abs( angle ) < half_fov;
}

inline Vector4 SelectASpawnPoint(Vector4 origin, std::vector<Vector4> vector4s_to_search, std::vector<Vector4> vector4s_to_exclude, uint max_range, uint min_range, Hash entity_model_hash) {
	Vector4 empty_spawn_point;
	std::vector<Vector4> filtered_vector4s;
	if (vector4s_to_search.size() == 0) { // don't bother continuing with an empty vector.
		Logger.Write("SelectASpawnPoint(): vector_of_vector4s_to_search was empty (function was provided an empty vector), returning an empty Vector4!", LogError);
		return empty_spawn_point; // remember, can't throw exceptions, ScripHookV intercepts them.
	}
	for (Vector4 this_vector4 : vector4s_to_search) {
		uint distance = GAMEPLAY::GET_DISTANCE_BETWEEN_COORDS(this_vector4.x, this_vector4.y, this_vector4.z, origin.x, origin.y, origin.z, 0);
		bool is_between_min_and_max_range = (min_range < distance && distance < max_range);
		bool occluded = ENTITY::WOULD_ENTITY_BE_OCCLUDED(entity_model_hash, this_vector4.x, this_vector4.y, this_vector4.z, true);
		bool occupied = GAMEPLAY::IS_POSITION_OCCUPIED(this_vector4.x, this_vector4.y, this_vector4.z, 3.5f, false, true, true, false, false, false, false);
		bool excluded = false;
		for (Vector4 exclude_vector4 : vector4s_to_exclude) {
			if (GAMEPLAY::GET_DISTANCE_BETWEEN_COORDS(exclude_vector4.x, exclude_vector4.y, exclude_vector4.z, this_vector4.x, this_vector4.y, this_vector4.z, 0) < 1.0)
				excluded = true;
		}
		if (is_between_min_and_max_range && !occluded && !occupied && !excluded) {
			filtered_vector4s.push_back(this_vector4);
		}
	}
	if (filtered_vector4s.size() == 0) { // don't bother continuing with an empty vector.
		Logger.Write("SelectASpawnPoint(): filtered_vector4s was empty (there were no points that met the criteria), returning an empty Vector4!", LogError);
		return empty_spawn_point; // remember, can't throw exceptions, ScripHookV intercepts them.
	}
	Vector4 selected_vector4 = filtered_vector4s[GetFromUniformIntDistribution(0, static_cast<int>(filtered_vector4s.size() - 1))]; // this should be a random point... also, yes a size_t *could* overflow an int, if I ever had more than 32,767 points available even after filtering. I suspect this is unlikely...
	uint distance = GAMEPLAY::GET_DISTANCE_BETWEEN_COORDS(selected_vector4.x, selected_vector4.y, selected_vector4.z, origin.x, origin.y, origin.z, 0);
	Logger.Write("SelectASpawnPoint(): selected from " + std::to_string(filtered_vector4s.size()) + " points using a given set of " + std::to_string(vector4s_to_search.size()) + " ( distance: " + std::to_string(distance) + " meters )", LogNormal);
	return selected_vector4; 
}

inline Hash SelectAVehicleModel(std::vector<Hash> vector_of_hashes_to_search, uint vehicle_class_options) {
	Logger.Write("SelectAVehicleModel()", LogVerbose);
	Hash empty_hash = NULL;
	std::vector<Hash> filtered_hashes;
	if (vector_of_hashes_to_search.size() == 0) {
		Logger.Write("SelectAVehicleModel(): vector_of_hashes_to_search was empty, returning an empty Hash!", LogError);
		return empty_hash;
	}
	for (Hash this_hash : vector_of_hashes_to_search) {
		if (vehicle_class_options & GetVehicleClassBitwiseFromHash(this_hash)) {
			filtered_hashes.push_back(this_hash);
		}
	}
	if (filtered_hashes.size() == 0) { // don't bother continuing with an empty vector.
		Logger.Write("SelectAVehicleModel(): filtered_hashes was empty (there were no models that met the criteria), returning a NULL Hash!", LogError);
		return empty_hash; // remember, can't throw exceptions, ScripHookV intercepts them.
	}
	Hash selected_hash = filtered_hashes[GetFromUniformIntDistribution(0, static_cast<int>(filtered_hashes.size() - 1))]; // this should be a random point... also, yes a size_t *could* overflow an int, if I ever had more than 32,767 points available even after filtering. I suspect this is unlikely...
	char *this_vehicle_display_name = VEHICLE::GET_DISPLAY_NAME_FROM_VEHICLE_MODEL(selected_hash);
	char *this_vehicle_string_literal = UI::_GET_LABEL_TEXT(this_vehicle_display_name);
	Logger.Write("SelectAVehicleModel(): selected a model from " + std::to_string(filtered_hashes.size()) + " valid hashes using a given set of " + std::to_string(vector_of_hashes_to_search.size()) + " ( " + std::string(this_vehicle_string_literal) + " )", LogNormal);
	return selected_hash;
}

inline Ped SpawnACrateGuard(Ped skin, Vector4 origin, float x_margin, float y_margin, float z_margin, char * weapon) {
	Logger.Write("SpawnACrateGuard()", LogVerbose);
	Vector4 spawn_point = origin;
	int tries = 0;
	while ( true ) {
		spawn_point.x = origin.x + GetFromUniformRealDistribution( -x_margin, x_margin );
		spawn_point.y = origin.y + GetFromUniformRealDistribution( -y_margin, y_margin );
		spawn_point.z = origin.z + GetFromUniformRealDistribution( -z_margin, z_margin );
		float ground_z_at_spawn_point = GetGroundZAtThisLocation( spawn_point );
		if ( ground_z_at_spawn_point == 9999 ) {
			Vector3 closest_vehicle_node;
			bool found_a_vehicle_node = PATHFIND::GET_CLOSEST_VEHICLE_NODE( spawn_point.x, spawn_point.y, spawn_point.z, &closest_vehicle_node, 1, 3.0f, 0.0f );
			Wait( 0 );
			if ( !found_a_vehicle_node ) Logger.Write( "SpawnACrateGuard() PATHFIND::GET_CLOSEST_VEHICLE_NODE() RETURNED FALSE!", LogError );
			std::stringstream ss10; ss10 << std::fixed << std::setprecision( 2 ) << "SpawnACrateGuard() PATHFIND::GET_CLOSEST_VEHICLE_NODE (" << closest_vehicle_node.x << ", " << closest_vehicle_node.y << ", " << closest_vehicle_node.z << ")"; Logger.Write( ss10.str(), LogDebug );
			spawn_point.x = closest_vehicle_node.x; spawn_point.y = closest_vehicle_node.y; spawn_point.z = closest_vehicle_node.z;
		} else {
			spawn_point.z = ground_z_at_spawn_point + 1;
		}
		//Vector3 gameplay_camera_location = CAM::GET_GAMEPLAY_CAM_COORD();
		if ( IsSpawnUnseenByPlayer( spawn_point, skin ) ) {
			break;
		} else if ( tries > 9 ) {
			Logger.Write( "SpawnACrateGuard(): Couldn't find a point out of sight of the player (after 100 tries)!", LogError );
		}
		//float bearing = NormalizeToDegrees(GetAngleBetween2DCoords(player_position.x, player_position.y, 0, 0) + player_position.h);
		/*float min_angle = CAM::GET_GAMEPLAY_CAM_FOV() / 2;
		float max_angle = 360 - CAM::GET_GAMEPLAY_CAM_FOV() / 2;
		Vector3 spawn = GetCoordinateByOriginBearingAndDistance(player_position, GetFromUniformRealDistribution(min_angle, max_angle), GetFromUniformRealDistribution(11, 33));
		UI::ADD_BLIP_FOR_COORD(spawn.x, spawn.y, spawn.z);*/

	}
	
	Ped bad_guy = PED::CREATE_PED(26, skin, spawn_point.x, spawn_point.y, spawn_point.z, 40, false, true);
	OBJECT::PLACE_OBJECT_ON_GROUND_PROPERLY(bad_guy);
	while (!ENTITY::DOES_ENTITY_EXIST(bad_guy)) Wait(0);
	PED::SET_PED_RELATIONSHIP_GROUP_HASH(bad_guy, GAMEPLAY::GET_HASH_KEY("HATES_PLAYER"));
	if (weapon = "SURPRISE_ME") {
		if (GetFromUniformRealDistribution(0, 1) <= (0.005)) weapon = "WEAPON_RAILGUN";
		else if (GetFromUniformRealDistribution(0, 1) <= (0.01)) weapon = "WEAPON_RPG";
		else if (GetFromUniformRealDistribution(0, 1) <= (0.02)) weapon = "WEAPON_GRENADELAUNCHER";
		else if (GetFromUniformRealDistribution(0, 1) <= (0.04)) weapon = "WEAPON_MG";
		else if (GetFromUniformRealDistribution(0, 1) <= (0.12)) weapon = "WEAPON_SNIPERRIFLE";
		else if (GetFromUniformRealDistribution(0, 1) <= (0.24)) weapon = "WEAPON_CARBINERIFLE";
		else if (GetFromUniformRealDistribution(0, 1) <= (0.33)) weapon = "WEAPON_SMG";
		else if (GetFromUniformRealDistribution(0, 1) <= (0.33)) weapon = "WEAPON_PUMPSHOTGUN";
		else if (GetFromUniformRealDistribution(0, 1) <= (0.24)) weapon = "WEAPON_PISTOL50";
		else if (GetFromUniformRealDistribution(0, 1) <= (0.24)) weapon = "WEAPON_COMBATPISTOL";
		else weapon = "WEAPON_PISTOL";
	}
	WEAPON::GIVE_DELAYED_WEAPON_TO_PED(bad_guy, GAMEPLAY::GET_HASH_KEY((char *)weapon), 1000, 1);
	WEAPON::SET_CURRENT_PED_WEAPON(bad_guy, GAMEPLAY::GET_HASH_KEY((char *)weapon), 1);
	float relative_bearing = GetAngleBetween2DCoords(spawn_point.x, spawn_point.y, origin.x, origin.y);
	Vector3 point_behind = GetCoordinateByOriginBearingAndDistance(spawn_point, relative_bearing - 180, 10);
	AI::TASK_TURN_PED_TO_FACE_COORD(bad_guy, point_behind.x, point_behind.y, point_behind.z, 120000);
	float distance = GAMEPLAY::GET_DISTANCE_BETWEEN_COORDS( spawn_point.x, spawn_point.y, spawn_point.z, origin.x, origin.y, origin.z, 0 );
	AI::TASK_WANDER_IN_AREA(bad_guy, spawn_point.x, spawn_point.y, spawn_point.z, distance, distance/2, 1);
	ENTITY::SET_ENTITY_INVINCIBLE(bad_guy, false);
	std::stringstream ss; ss << std::fixed << std::setprecision(2) << "SpawnACrateGuard(): Ped spawned at ( " << spawn_point.x << ", " << spawn_point.y << ", " << spawn_point.z << " )"; Logger.Write(ss.str(), LogDebug);
	return bad_guy;
}

inline void SafelyRemoveBlip( Blip *blip ) {
	if ( UI::DOES_BLIP_EXIST( *blip ) ) UI::REMOVE_BLIP( blip );
	Wait( 0 );
}

inline void SafelyCleanupEntity( Entity *entity ) {
	Blip blip = UI::GET_BLIP_FROM_ENTITY( *entity );
	SafelyRemoveBlip( &blip );
	if ( DoesEntityExistAndIsNotNull( *entity ) ) {
		ENTITY::SET_ENTITY_AS_MISSION_ENTITY( *entity, false, false );
		ENTITY::SET_ENTITY_AS_NO_LONGER_NEEDED( entity );
	}
	Wait( 0 );
}

class CrateDropMission {
public:
	MissionType Prepare();
	MissionType Execute();
	MissionType Timeout();
private:
	enum CrateDropMissionStage { Approach, Fighting, Cleanup };
	CrateDropMissionStage current_stage_;
	Blip crate_blip_ = NULL;
	Vector4 crate_spawn_location_;
	bool crate_is_special_ = false;
	Object crate_;
	std::set<Ped> guards_;
	Ped skin_ = GAMEPLAY::GET_HASH_KEY("mp_g_m_pros_01");
	Hash crate_hash_ = GAMEPLAY::GET_HASH_KEY("prop_box_ammo04a");
	int max_ped_alertness_ = 0;
	ULONGLONG respawn_timer_ = 0;
	uint respawn_quantity_ = 0;
};

MissionType CrateDropMission::Prepare() {
	Logger.Write("CrateDropMission::Prepare()", LogNormal);
	// Someday I'll figure out something to expand this. Not today.
	std::vector<Vector4> empty_vector;
	crate_spawn_location_ = SelectASpawnPoint(player_position, crate_spawn_points, empty_vector, spawn_point_maximum_range, spawn_point_minimum_range, NULL);
	Logger.Write("CrateDropMission::Prepare(): crate_spawn_location: ( " + std::to_string(crate_spawn_location_.x) + ", " + std::to_string(crate_spawn_location_.y) + " )", LogNormal);
	if (crate_spawn_location_.x == 0.0f && crate_spawn_location_.y == 0.0f && crate_spawn_location_.z == 0.0f && crate_spawn_location_.h == 0.0f) return NO_Mission;
	if (GetFromUniformRealDistribution(0, 1) <= (0.05)) crate_is_special_ = true; else crate_is_special_ = false;
	crate_blip_ = UI::ADD_BLIP_FOR_COORD(crate_spawn_location_.x, crate_spawn_location_.y, crate_spawn_location_.z);
	if (use_default_blip) UI::SET_BLIP_SPRITE(crate_blip_, 1); 
	else UI::SET_BLIP_SPRITE(crate_blip_, 306); 
	UI::SET_BLIP_SCALE(crate_blip_, 1.5);
	if (crate_is_special_) UI::SET_BLIP_COLOUR(crate_blip_, 5);
	else UI::SET_BLIP_COLOUR(crate_blip_, 2);
	UI::SET_BLIP_DISPLAY(crate_blip_, (char)"you will never see this");
	if (crate_is_special_) CreateNotification("A ~y~special crate~w~ has been dropped.", play_notification_beeps);
	else CreateNotification("A ~g~crate~w~ has been dropped.", play_notification_beeps);
	STREAMING::REQUEST_MODEL(skin_);
	while (!STREAMING::HAS_MODEL_LOADED(skin_)) Wait(0);
	current_mission_objective = crate_spawn_location_;
	current_stage_ = Approach;
	return CrateDrop;
}

MissionType CrateDropMission::Execute() {
	if ( current_stage_ == Approach ) {
		if ( GAMEPLAY::GET_DISTANCE_BETWEEN_COORDS( player_position.x, player_position.y, player_position.z, crate_spawn_location_.x, crate_spawn_location_.y, crate_spawn_location_.z, 0 ) < 300 ) {
			Logger.Write( "CrateDropMission::Execute(): Creating objects...", LogVerbose );
			STREAMING::REQUEST_MODEL( GAMEPLAY::GET_HASH_KEY( "prop_box_ammo04a" ) );
			while ( !STREAMING::HAS_MODEL_LOADED( GAMEPLAY::GET_HASH_KEY( "prop_box_ammo04a" ) ) ) Wait( 0 );
			crate_ = OBJECT::CREATE_AMBIENT_PICKUP( 0x14568F28, crate_spawn_location_.x, crate_spawn_location_.y, crate_spawn_location_.z + 1, -1, 0, crate_hash_, 1, 1 );
			ENTITY::SET_ENTITY_ALPHA( crate_, 255, 1 );
			STREAMING::REQUEST_COLLISION_FOR_MODEL( crate_hash_ );
			while ( !STREAMING::HAS_COLLISION_FOR_MODEL_LOADED( crate_hash_ ) ) Wait( 0 );
			ENTITY::SET_ENTITY_COLLISION( crate_, true, true ); // 1A9205C1B9EE827F 139FD37D
																													//while (ENTITY::GET_ENTITY_HEIGHT(crate_, crate_spawn_location_.x, crate_spawn_location_.y, crate_spawn_location_.z, 1, 1) > 0) OBJECT::PLACE_OBJECT_ON_GROUND_PROPERLY(crate_);
																													//ENTITY::FREEZE_ENTITY_POSITION(crate_, 1);
			SafelyRemoveBlip( &crate_blip_ );
			crate_blip_ = UI::ADD_BLIP_FOR_ENTITY( crate_ );
			if ( use_default_blip ) UI::SET_BLIP_SPRITE( crate_blip_, 1 );
			else UI::SET_BLIP_SPRITE( crate_blip_, 306 );
			UI::SET_BLIP_SCALE( crate_blip_, 1.5 );
			if ( crate_is_special_ ) {
				UI::SET_BLIP_COLOUR( crate_blip_, 5 );
				Logger.Write( "CrateDropMission::Execute(): crate is special", LogNormal );
			} else UI::SET_BLIP_COLOUR( crate_blip_, 2 );
			UI::SET_BLIP_DISPLAY( crate_blip_, ( char )"you will never see this" );
			STREAMING::REQUEST_MODEL( skin_ );
			//Logger.Write("CrateDropMission::Execute(): requested model of bad guy", LogNormal);
			while ( !STREAMING::HAS_MODEL_LOADED( skin_ ) ) Wait( 0 );
			//Logger.Write("CrateDropMission::Execute(): model has loaded", LogNormal);
			uint number_of_guards = number_of_guards_to_spawn;
			if ( crate_is_special_ ) number_of_guards = round( number_of_guards_to_spawn * 1.5 );
			for ( uint i = 0; i < number_of_guards; i++ ) {
				Logger.Write( "CrateDropMission::Execute(): spawning a bad guy", LogNormal );
				guards_.insert( SpawnACrateGuard( skin_, crate_spawn_location_, 11, 11, 11, "SURPRISE_ME" ) );
			}

			Logger.Write( "CrateDropMission::Execute(): guards were spawned", LogNormal );
			current_stage_ = Fighting;
		}
	}
	if ( current_stage_ == Fighting ) {
		for ( Ped guard : guards_ ) {
			if ( PED::GET_PED_ALERTNESS( guard ) < max_ped_alertness_ ) PED::SET_PED_ALERTNESS( guard, max_ped_alertness_ ); // alert each guard to highest level among them
			max_ped_alertness_ = std::max( max_ped_alertness_, PED::GET_PED_ALERTNESS( guard ) );
			
			if ( PED::IS_PED_DEAD_OR_DYING( guard, 1 ) ) { // death cleanup
				guards_.erase( guard );
				SafelyCleanupEntity( &guard );
				respawn_quantity_ += 1;
				if ( respawn_timer_ == 0 ) {
					respawn_timer_ = GetTickCount64() + time_between_guard_respawns;
				} else {
					respawn_timer_ += time_between_guard_respawns;
				}
				Logger.Write("CrateDropMission::Execute(): Guard is dead or dying, respawn in " + std::to_string(respawn_timer_ - GetTickCount64()) + " milliseconds", LogDebug);
			}
			Blip blip = UI::GET_BLIP_FROM_ENTITY(guard);
			if ( ENTITY::HAS_ENTITY_CLEAR_LOS_TO_ENTITY_IN_FRONT( player_ped, guard ) ) { // blip visibility add/remove
				if ( !UI::DOES_BLIP_EXIST( blip ) ) {
					Blip blip = UI::ADD_BLIP_FOR_ENTITY( guard );
					UI::SET_BLIP_SCALE( blip, 0.75f );
				}
			} else SafelyRemoveBlip( &blip );
		}
		int difference = GetTickCount64() - respawn_timer_;
		//std::stringstream ss; ss << "CrateDropMission::Execute(): GetTickCount64(): " << GetTickCount64() << ", respawn_timer_: " << respawn_timer_ << ", difference: " << difference;
		//Logger.Write(ss.str(), LogDebug);
		if ( difference > 0 && respawn_quantity_ > 0 ) {
			Logger.Write("CrateDropMission::Execute(): respawn_timer_ has elapsed. respawn_quantiy_ is: " + std::to_string(respawn_quantity_), LogDebug);
			respawn_timer_ = 0;
			for (uint i = 0; i < respawn_quantity_; i++) {
				STREAMING::REQUEST_MODEL(skin_);
				while (!STREAMING::HAS_MODEL_LOADED(skin_)) Wait(0);
				guards_.insert(SpawnACrateGuard(skin_, crate_spawn_location_, 33, 33, 33, "SURPRISE_ME"));
			}
			respawn_quantity_ = 0;
		}

		if ( !ENTITY::DOES_ENTITY_EXIST( crate_ ) ) current_stage_ = Cleanup;
	}
	if ( current_stage_ == Cleanup ) {
		for ( Ped guard : guards_ ) SafelyCleanupEntity( &guard );
		guards_.clear();
		if ( crate_is_special_ ) ChangeMoneyForCurrentPlayer( GetFromUniformIntDistribution( 50000, 150000 ), mission_reward_modifier );
		else ChangeMoneyForCurrentPlayer( GetFromUniformIntDistribution( 25000, 75000 ), mission_reward_modifier );
		CreateNotification( "The drop was acquired.", play_notification_beeps );
		current_mission_objective = Vector4{ 999,999,999,999 };
		return NO_Mission;
	}
	return CrateDrop;	
}

MissionType CrateDropMission::Timeout() {
	uint distance_to_crate = GAMEPLAY::GET_DISTANCE_BETWEEN_COORDS(player_position.x, player_position.y, player_position.z, crate_spawn_location_.x, crate_spawn_location_.y, crate_spawn_location_.z, 0);
	if (distance_to_crate > mission_minimum_range_for_timeout || ENTITY::IS_ENTITY_DEAD(player_ped)) {
		SafelyRemoveBlip(&crate_blip_);
		SafelyCleanupEntity( &crate_ );
		for (Ped guard : guards_) {
			SafelyCleanupEntity( &guard );
		}
		CreateNotification("The ~g~crate~w~ has been claimed by smugglers.", play_notification_beeps);
		current_mission_objective = Vector4{ 999,999,999,999 };
		return NO_Mission;
	}
	return CrateDrop;
}

class ArmoredTruckMission {
public:
	MissionType Prepare();
	MissionType Execute();
	MissionType Timeout();
private:
	Vehicle armored_truck_ = NULL;
	Ped truck_driver_ = NULL;
	Ped truck_passenger_ = NULL;
	Blip truck_blip_ = NULL;
	Hash truck_model_ = GAMEPLAY::GET_HASH_KEY("stockade");
	Ped skin_ = GAMEPLAY::GET_HASH_KEY("mp_s_m_armoured_01");
	bool truck_was_set_on_ground_properly_ = false;
	int DEBUG_entity_health_warn_amount = 1000;
	int DEBUG_vehicle_body_health_2_warn_amount = 100;
	int DEBUG_report_player_ped_is_in_combat_with_driver = true;
	int DEBUG_report_driver_is_in_combat_with_player_ped = true;
	int DEBUG_report_driver_is_fleeing = true;
	int DEBUG_report_driver_alertness_level = -1;
};

MissionType ArmoredTruckMission::Prepare() {
	Logger.Write("ArmoredTruckMission::Prepare()", LogNormal);
	Vector4 vehicle_spawn_position = SelectASpawnPoint(player_position, vehicle_spawn_points, reserved_vehicle_spawn_points, spawn_point_maximum_range, spawn_point_minimum_range, NULL);
	if (vehicle_spawn_position.x == 0.0f && vehicle_spawn_position.y == 0.0f && vehicle_spawn_position.z == 0.0f && vehicle_spawn_position.h == 0.0f) return NO_Mission;
	Vector3 closest_vehicle_node; float closest_vehicle_node_heading;
	PATHFIND::GET_CLOSEST_VEHICLE_NODE_WITH_HEADING(vehicle_spawn_position.x, vehicle_spawn_position.y, vehicle_spawn_position.z, &closest_vehicle_node, &closest_vehicle_node_heading, 1, 3.0f, 0.0f);
	Wait(0);
	vehicle_spawn_position.x = closest_vehicle_node.x; vehicle_spawn_position.y = closest_vehicle_node.y; vehicle_spawn_position.z = closest_vehicle_node.z; vehicle_spawn_position.h = closest_vehicle_node_heading;
	STREAMING::REQUEST_MODEL(truck_model_);
	while (!STREAMING::HAS_MODEL_LOADED(truck_model_)) Wait(0);
	armored_truck_ = VEHICLE::CREATE_VEHICLE(truck_model_, vehicle_spawn_position.x, vehicle_spawn_position.y, vehicle_spawn_position.z, vehicle_spawn_position.h, 0, 0);
	STREAMING::REQUEST_MODEL(skin_);
	while (!STREAMING::HAS_MODEL_LOADED(skin_)) Wait(0);
	truck_driver_ = PED::CREATE_PED_INSIDE_VEHICLE(armored_truck_, 26, skin_, -1, false, false);
	if (GetFromUniformRealDistribution(0, 1) <= (0.5)) truck_passenger_ = PED::CREATE_PED_INSIDE_VEHICLE(armored_truck_, 26, skin_, 0, false, false);
	VEHICLE::SET_VEHICLE_DOORS_LOCKED(armored_truck_, 2);
	Wait(0);
	if (ENTITY::DOES_ENTITY_EXIST(truck_driver_)) AI::TASK_VEHICLE_DRIVE_WANDER(truck_driver_, armored_truck_, 10.0f, 153);
	truck_blip_ = UI::ADD_BLIP_FOR_ENTITY(armored_truck_);
	if (use_default_blip) UI::SET_BLIP_SPRITE(truck_blip_, 1);
	else UI::SET_BLIP_SPRITE(truck_blip_, 67);
	UI::SET_BLIP_COLOUR(truck_blip_, 3);
	UI::SET_BLIP_DISPLAY(truck_blip_, (char)"you will never see this");
	CreateNotification("An ~b~armored truck~w~ has been spotted carrying cash.", play_notification_beeps);

	{
		ENTITY::SET_ENTITY_MAX_HEALTH(armored_truck_, 3000);
		ENTITY::SET_ENTITY_HEALTH(armored_truck_, 3000);
		DEBUG_entity_health_warn_amount = 3000;
		DEBUG_report_player_ped_is_in_combat_with_driver = true;
		DEBUG_report_driver_is_in_combat_with_player_ped = true;
		DEBUG_report_driver_is_fleeing = true;
		int DEBUG_report_driver_alertness_level = -1;
		Logger.Write("ArmoredTruckMission::Prepare(): ENTITY::GET_ENTITY_HEALTH(armored_truck_): " + std::to_string(ENTITY::GET_ENTITY_HEALTH(armored_truck_)), LogDebug);
	}
	return ArmoredTruck;
}

MissionType ArmoredTruckMission::Execute() {
	Vector4 truck_position = GetVector4OfEntity(armored_truck_);
	uint distance_to_truck = GAMEPLAY::GET_DISTANCE_BETWEEN_COORDS(truck_position.x, truck_position.y, truck_position.z, player_position.x, player_position.y, player_position.z, 0);
	
	if (ENTITY::GET_ENTITY_HEALTH(armored_truck_) < DEBUG_entity_health_warn_amount) {
		Logger.Write("ArmoredTruckMission::Execute(): DEBUG_entity_health_warn_amount < " + std::to_string(DEBUG_entity_health_warn_amount) + ", current value:" + std::to_string(ENTITY::GET_ENTITY_HEALTH(armored_truck_)), LogVerbose);
		DEBUG_entity_health_warn_amount -= 1000;
	}

	if (PED::IS_PED_FLEEING(truck_driver_) && DEBUG_report_driver_is_fleeing) {
		Logger.Write("ArmoredTruckMission::Execute(): PED::IS_PED_FLEEING(truck_driver_) == true", LogDebug);
		DEBUG_report_driver_is_fleeing = false;

	}
	if (PED::GET_PED_ALERTNESS(truck_driver_) > DEBUG_report_driver_alertness_level) {
		Logger.Write("ArmoredTruckMission::Execute(): PED::GET_PED_ALERTNESS(truck_driver_) == " + std::to_string(PED::GET_PED_ALERTNESS(truck_driver_)), LogDebug);
		DEBUG_report_driver_alertness_level += 1;
	}

	//if (ENTITY::HAS_ENTITY_BEEN_DAMAGED_BY_ENTITY(armored_truck_, player_ped, 1)) {
	//	Logger.Write("ArmoredTruckMission::Execute(): ENTITY::HAS_ENTITY_BEEN_DAMAGED_BY_ENTITY(armored_truck_, player_ped, 1) == true", LogDebug);
	//}
	//if (VEHICLE::_GET_VEHICLE_BODY_HEALTH_2(armored_truck_) < DEBUG_vehicle_body_health_2_warn_amount) {
	//	Logger.Write("ArmoredTruckMission::Execute(): DEBUG_vehicle_body_health_2_warn_amount < " + std::to_string(DEBUG_vehicle_body_health_2_warn_amount) + ", current value:" + std::to_string(VEHICLE::_GET_VEHICLE_BODY_HEALTH_2(armored_truck_)), LogVerbose);
	//	DEBUG_vehicle_body_health_2_warn_amount -= 5;
	//}
	//if (PED::IS_PED_IN_COMBAT(player_ped, truck_driver_) && DEBUG_report_player_ped_is_in_combat_with_driver) {
	//	Logger.Write("ArmoredTruckMission::Execute(): PED::IS_PED_IN_COMBAT(player_ped, truck_driver_) == true", LogDebug);
	//	DEBUG_report_player_ped_is_in_combat_with_driver = false;
	//}
	//if (PED::IS_PED_IN_COMBAT(truck_driver_, player_ped) && DEBUG_report_driver_is_in_combat_with_player_ped) {
	//	Logger.Write("ArmoredTruckMission::Execute(): PED::IS_PED_IN_COMBAT(truck_driver_, player_ped) == true", LogDebug);
	//	DEBUG_report_driver_is_in_combat_with_player_ped = false;
	//}

	if (!VEHICLE::IS_VEHICLE_DRIVEABLE(armored_truck_, 1)) {
		CreateNotification("The ~b~armored truck~w~ has been destroyed. The cash cases inside have been ruined.", play_notification_beeps);
		SafelyCleanupEntity( &truck_driver_ );
		SafelyCleanupEntity( &armored_truck_ );
		return NO_Mission;
	}

	//if (PED::IS_PED_FATALLY_INJURED(truck_driver_) || !PED::IS_PED_IN_VEHICLE(truck_driver_, armored_truck_, 0)) {
	//	VEHICLE::SET_VEHICLE_DOORS_LOCKED(armored_truck_, 7);
	//}

	if (!PED::IS_PED_IN_VEHICLE(truck_driver_, armored_truck_, 0)) {
		VEHICLE::SET_VEHICLE_DOORS_LOCKED(armored_truck_, 7);
	}
	if (ENTITY::GET_ENTITY_HEALTH(armored_truck_) < 500) {
		VEHICLE::SET_VEHICLE_DOOR_CONTROL(armored_truck_, 2, 3, 0.666);
		VEHICLE::SET_VEHICLE_DOOR_CONTROL(armored_truck_, 3, 3, 0.666);
	}
	if (PED::IS_PED_IN_VEHICLE(player_ped, armored_truck_, 0)) {
		Wait(125);
		VEHICLE::SET_VEHICLE_DOOR_CONTROL(armored_truck_, 2, 3, 0.666);
		VEHICLE::SET_VEHICLE_DOOR_CONTROL(armored_truck_, 3, 3, 0.666);
	}
	if ((VEHICLE::GET_VEHICLE_DOOR_ANGLE_RATIO(armored_truck_, 2) > 0.333) || (VEHICLE::GET_VEHICLE_DOOR_ANGLE_RATIO(armored_truck_, 3) > 0.333)) {
		Pickup case1; Pickup case2; Pickup case3;
		Vector3 behind_truck = GetCoordinateByOriginBearingAndDistance(truck_position, truck_position.h+270, 4.44f);
		int random = rand() % 3;
		switch (random) { // falls through!
		case 0:
			SetPlayerMinimumWantedLevel(Wanted_Three);
			case3 = OBJECT::CREATE_AMBIENT_PICKUP(0xDE78F17E, behind_truck.x-0.25, behind_truck.y-0.25, behind_truck.z + 1, -1, GetFromUniformIntDistribution(5, 25) * 1000 * mission_reward_modifier, 0, 1, 1);
			SafelyCleanupEntity( &case3 );
		case 1:
			SetPlayerMinimumWantedLevel(Wanted_Two);
			case2 = OBJECT::CREATE_AMBIENT_PICKUP(0xDE78F17E, behind_truck.x+0.25, behind_truck.y+0.25, behind_truck.z + 1, -1, GetFromUniformIntDistribution(5, 25) * 1000 * mission_reward_modifier, 0, 1, 1);
			SafelyCleanupEntity( &case2 );
		case 2:
			SetPlayerMinimumWantedLevel(Wanted_One);
			case1 = OBJECT::CREATE_AMBIENT_PICKUP(0xDE78F17E, behind_truck.x, behind_truck.y, behind_truck.z + 1, -1, GetFromUniformIntDistribution(5, 25) * 1000 * mission_reward_modifier, 0, 1, 1);
			SafelyCleanupEntity( &case1 );
		}
		CreateNotification("The ~b~armored truck's~w~ doors have been opened. Cash has been dropped.", play_notification_beeps);
		SafelyCleanupEntity( &truck_driver_ );
		SafelyCleanupEntity( &armored_truck_ );
		return NO_Mission;
	}
	current_mission_objective = truck_position;
	return ArmoredTruck;
}

MissionType ArmoredTruckMission::Timeout() {
	Vector3 truck_coordinates = ENTITY::GET_ENTITY_COORDS(armored_truck_, 0);
	uint distance_to_truck = GAMEPLAY::GET_DISTANCE_BETWEEN_COORDS(truck_coordinates.x, truck_coordinates.y, truck_coordinates.z, player_position.x, player_position.y, player_position.z, 0);
	if (distance_to_truck > mission_minimum_range_for_timeout || ENTITY::IS_ENTITY_DEAD(player_ped)) {
		SafelyCleanupEntity( &truck_driver_ );
		SafelyCleanupEntity( &armored_truck_ );
		CreateNotification("The ~b~armored truck~w~ has finished carrying cash.", play_notification_beeps);
		current_mission_objective = Vector4{ 999,999,999,999 };
		return NO_Mission;
	}
	return ArmoredTruck;
}

class AssassinationMission {
public:
	MissionType Prepare();
	MissionType Execute();
	MissionType Timeout();
private:
	Ped assassination_target_ = NULL;
	Blip target_blip_ = NULL;
};

MissionType AssassinationMission::Prepare() {
	Logger.Write("AssassinationMission::Prepare()", LogNormal);
	Vector4 target_spawn_position = SelectASpawnPoint(player_position, vehicle_spawn_points, reserved_vehicle_spawn_points, spawn_point_maximum_range, spawn_point_minimum_range, NULL);
	if (target_spawn_position.x == 0.0f && target_spawn_position.y == 0.0f && target_spawn_position.z == 0.0f && target_spawn_position.h == 0.0f) return NO_Mission;
	assassination_target_ = PED::CREATE_RANDOM_PED(target_spawn_position.x, target_spawn_position.y, target_spawn_position.z);
	while (!ENTITY::DOES_ENTITY_EXIST(assassination_target_)) Wait(0);
	PED::SET_PED_DESIRED_HEADING(assassination_target_, target_spawn_position.h);
	target_blip_ = UI::ADD_BLIP_FOR_ENTITY(assassination_target_);
	AI::TASK_WANDER_STANDARD(assassination_target_, 1000.0f, 0);
	if (use_default_blip) UI::SET_BLIP_SPRITE(target_blip_, 1);
	else UI::SET_BLIP_SPRITE(target_blip_, 432);
	UI::SET_BLIP_COLOUR(target_blip_, 1);
	UI::SET_BLIP_DISPLAY(target_blip_, (char)"you will never see this");
	CreateNotification("A hit has been placed on a ~r~target~w~.", play_notification_beeps);
	return Assassination;
}

MissionType AssassinationMission::Execute() {
	Vector4 target_position = GetVector4OfEntity(assassination_target_);
	if (ENTITY::IS_ENTITY_DEAD(assassination_target_)) {
		SafelyCleanupEntity( &assassination_target_ );
		ChangeMoneyForCurrentPlayer(GetFromUniformIntDistribution(5, 25) * 1000, mission_reward_modifier);
		CreateNotification("The ~r~target~w~ has been eliminated.", play_notification_beeps);
		current_mission_objective = Vector4{ 999,999,999,999 };
		return NO_Mission;
	}
	current_mission_objective = target_position;
	return Assassination;
}

MissionType AssassinationMission::Timeout() {
	Vector3 target_coordinates = ENTITY::GET_ENTITY_COORDS(assassination_target_, 0);
	uint distance_to_target = GAMEPLAY::GET_DISTANCE_BETWEEN_COORDS(target_coordinates.x, target_coordinates.y, target_coordinates.z, player_position.x, player_position.y, player_position.z, 0);
	if (distance_to_target > mission_minimum_range_for_timeout || ENTITY::IS_ENTITY_DEAD(player_ped)) {
		CreateNotification("The contract on the ~r~target~w~ has expired.", play_notification_beeps);
		SafelyCleanupEntity( &assassination_target_ );
		current_mission_objective = Vector4{ 999,999,999,999 };
		return NO_Mission;
	}
	return Assassination;
}

class DestroyVehicleMission {
public:
	MissionType Prepare();
	MissionType Execute();
	MissionType Timeout();
private:
	Vehicle vehicle_to_destroy_ = NULL;
	Blip vehicle_blip_ = NULL;
};

MissionType DestroyVehicleMission::Prepare() {
	Logger.Write("DestroyVehicleMission::Prepare()", LogNormal);
	Vector4 vehicle_spawn_position = SelectASpawnPoint(player_position, vehicle_spawn_points, reserved_vehicle_spawn_points, spawn_point_maximum_range, spawn_point_minimum_range, NULL);
	if (vehicle_spawn_position.x == 0.0f && vehicle_spawn_position.y == 0.0f && vehicle_spawn_position.z == 0.0f && vehicle_spawn_position.h == 0.0f) return NO_Mission;
	Hash vehicle_hash = SelectAVehicleModel(possible_vehicle_models, destroyable_vehicle_classes);
	if (vehicle_hash == NULL) return NO_Mission;
	STREAMING::REQUEST_MODEL(vehicle_hash);
	while (!STREAMING::HAS_MODEL_LOADED(vehicle_hash)) Wait(0);
	vehicle_to_destroy_ = VEHICLE::CREATE_VEHICLE(vehicle_hash, vehicle_spawn_position.x, vehicle_spawn_position.y, vehicle_spawn_position.z, vehicle_spawn_position.h, 0, 0);
	VEHICLE::SET_VEHICLE_ON_GROUND_PROPERLY(vehicle_to_destroy_);
	vehicle_blip_ = UI::ADD_BLIP_FOR_ENTITY(vehicle_to_destroy_);
	if (use_default_blip) UI::SET_BLIP_SPRITE(vehicle_blip_, 1);
	else if (VEHICLE::IS_THIS_MODEL_A_CAR(vehicle_hash)) UI::SET_BLIP_SPRITE(vehicle_blip_, 225);
	else UI::SET_BLIP_SPRITE(vehicle_blip_, 226);
	UI::SET_BLIP_COLOUR(vehicle_blip_, 1);
	UI::SET_BLIP_DISPLAY(vehicle_blip_, (char)"you will never see this");
	CreateNotification("A ~r~smuggler's vehicle~w~ has been identified.", play_notification_beeps);
	current_mission_objective = GetVector4OfEntity(vehicle_to_destroy_);
	return DestroyVehicle;
}

MissionType DestroyVehicleMission::Execute() {
	if (!VEHICLE::IS_VEHICLE_DRIVEABLE(vehicle_to_destroy_, 1)) {
		SafelyCleanupEntity( &vehicle_to_destroy_ );
		ChangeMoneyForCurrentPlayer(GetFromUniformIntDistribution(5, 25) * 1000, mission_reward_modifier);
		CreateNotification("~r~Vehicle~w~ destroyed.", play_notification_beeps);
		return NO_Mission;
	}
	return DestroyVehicle;
}

MissionType DestroyVehicleMission::Timeout() {
	Vector3 vehicle_coordinates = ENTITY::GET_ENTITY_COORDS(vehicle_to_destroy_, 0);
	uint vehicleDistance = GAMEPLAY::GET_DISTANCE_BETWEEN_COORDS(vehicle_coordinates.x, vehicle_coordinates.y, vehicle_coordinates.z, player_position.x, player_position.y, player_position.z, 0);
	if (vehicleDistance > mission_minimum_range_for_timeout || ENTITY::IS_ENTITY_DEAD(player_ped)) {
		CreateNotification("The ~r~smuggler vehicle~w~ has been claimed by smugglers.", play_notification_beeps);
		SafelyCleanupEntity( &vehicle_to_destroy_ );
		current_mission_objective = Vector4{ 999,999,999,999 };
		return NO_Mission;
	}
	return DestroyVehicle;
}

class StealVehicleMission {
public:
	MissionType Prepare();
	MissionType Execute();
	MissionType Timeout();
private:
	int stealable_vehicle_flags;
	Vehicle vehicle_to_steal_ = NULL;
	Hash vehicle_hash_;
	Vector3 drop_off_coordinates;
	Blip vehicle_blip_ = NULL;
	Blip drop_off_blip_ = NULL;
	int vehicle_primary_color_initial_, vehicle_secondary_color_initial_;
	int vehicle_primary_color_compare_, vehicle_secondary_color_compare_;
	enum StealVehicleMissionStage { GetCar, ResprayCar, FixCar, DeliverCar };
	StealVehicleMissionStage mission_objective_;
	bool vehicle_must_be_undamaged_;
};

MissionType StealVehicleMission::Prepare() {
	Logger.Write("StealVehicleMission.Prepare()", LogNormal);
	drop_off_coordinates.x = 1226.06; drop_off_coordinates.y = -3231.36; drop_off_coordinates.z = 5.02;
	
	stealable_vehicle_flags = stealable_vehicle_classes; // we want some sort of weighted distribution of cars to steal...
	if (GetFromUniformRealDistribution(0, 1) <= (0.6666)) stealable_vehicle_flags = stealable_vehicle_flags & ~Super; // disallow Supers 66% of the time.
	if (GetFromUniformRealDistribution(0, 1) <= (0.3333)) stealable_vehicle_flags = stealable_vehicle_flags & ~Sports; // disallow Sports 33% of the time.
	if (GetFromUniformRealDistribution(0, 1) <= (0.3333)) stealable_vehicle_flags = stealable_vehicle_flags & ~SportsClassic; // etc...
	if (GetFromUniformRealDistribution(0, 1) <= (0.3333)) stealable_vehicle_flags = stealable_vehicle_flags & ~OffRoad; // etc...
	vehicle_hash_ = SelectAVehicleModel(possible_vehicle_models, stealable_vehicle_flags);
	if (vehicle_hash_ == NULL) return NO_Mission;
	Vector4 vehicle_spawn_position = SelectASpawnPoint(player_position, vehicle_spawn_points, reserved_vehicle_spawn_points, spawn_point_maximum_range, spawn_point_minimum_range, vehicle_hash_);
	if (vehicle_spawn_position.x == 0.0f && vehicle_spawn_position.y == 0.0f && vehicle_spawn_position.z == 0.0f && vehicle_spawn_position.h == 0.0f) return NO_Mission;
	
	STREAMING::REQUEST_MODEL(vehicle_hash_); // oh wait no, we can just load it.
	while (!STREAMING::HAS_MODEL_LOADED(vehicle_hash_)) Wait(0);
	vehicle_to_steal_ = VEHICLE::CREATE_VEHICLE(vehicle_hash_, vehicle_spawn_position.x, vehicle_spawn_position.y, vehicle_spawn_position.z, vehicle_spawn_position.h, 0, 0);
	VEHICLE::SET_VEHICLE_ON_GROUND_PROPERLY(vehicle_to_steal_);
	if (GetFromUniformRealDistribution(0, 1) <= (0.6666)) VEHICLE::SET_VEHICLE_DOORS_LOCKED(vehicle_to_steal_, 7);
	if (GetFromUniformRealDistribution(0, 1) <= (0.6666)) VEHICLE::SET_VEHICLE_NEEDS_TO_BE_HOTWIRED(vehicle_to_steal_, true);
	vehicle_blip_ = UI::ADD_BLIP_FOR_ENTITY(vehicle_to_steal_);
	if (use_default_blip) UI::SET_BLIP_SPRITE(vehicle_blip_, 1);
	else if (VEHICLE::IS_THIS_MODEL_A_CAR(vehicle_hash_)) UI::SET_BLIP_SPRITE(vehicle_blip_, 225);
	else UI::SET_BLIP_SPRITE(vehicle_blip_, 226);
	UI::SET_BLIP_COLOUR(vehicle_blip_, 5);
	UI::SET_BLIP_DISPLAY(vehicle_blip_, (char)"you will never see this");
	VEHICLE::GET_VEHICLE_COLOURS(vehicle_to_steal_, &vehicle_primary_color_initial_, &vehicle_secondary_color_initial_);
	if (GetFromUniformRealDistribution(0, 1) <= (0.1666)) {
		vehicle_must_be_undamaged_ = true;
		CreateNotification("A special ~y~vehicle~w~ has been requested for retrieval.", play_notification_beeps);
		Wait(1500);
		CreateNotification("Additionally, the client requested that it be delivered without damage.", play_notification_beeps);
	} else {
		vehicle_must_be_undamaged_ = false;
		CreateNotification("A special ~y~vehicle~w~ has been requested for retrieval.", play_notification_beeps);
	}
	mission_objective_ = GetCar;
	current_mission_objective = GetVector4OfEntity(vehicle_to_steal_);
	return StealVehicle;
}

MissionType StealVehicleMission::Execute() {
	if (!VEHICLE::IS_VEHICLE_DRIVEABLE(vehicle_to_steal_, 1)) {
		CreateNotification("The ~y~vehicle~w~ has been destroyed.", play_notification_beeps);
		if (mission_objective_ == DeliverCar) SafelyRemoveBlip(&drop_off_blip_);
		SafelyCleanupEntity( &vehicle_to_steal_ );
		current_mission_objective = Vector4{ 999,999,999,999 };
		return NO_Mission;
	}

	if (mission_objective_ == GetCar) {
		if (PED::IS_PED_IN_VEHICLE(player_ped, vehicle_to_steal_, 0)) {
			if (GetVehicleClassBitwiseFromHash(vehicle_hash_) == Super && GetFromUniformRealDistribution(0, 1) <= (0.3333)) SetPlayerMinimumWantedLevel(Wanted_Three);
			else if (GetFromUniformRealDistribution(0, 1) <= (0.3333)) SetPlayerMinimumWantedLevel(Wanted_Two);
			else if (GetFromUniformRealDistribution(0, 1) <= (0.6666)) SetPlayerMinimumWantedLevel(Wanted_One);
			CreateNotification("Respray the ~y~vehicle~w~ before turning it in.", play_notification_beeps);
			mission_objective_ = ResprayCar;
		}
	}
	
	if (mission_objective_ == ResprayCar) {
		VEHICLE::GET_VEHICLE_COLOURS(vehicle_to_steal_, &vehicle_primary_color_compare_, &vehicle_secondary_color_compare_);
		if (vehicle_primary_color_initial_ != vehicle_primary_color_compare_ && ENTITY::GET_ENTITY_SPEED(vehicle_to_steal_) != 0 ||
			vehicle_secondary_color_initial_ != vehicle_secondary_color_compare_ && ENTITY::GET_ENTITY_SPEED(vehicle_to_steal_) != 0) {
			drop_off_blip_ = UI::ADD_BLIP_FOR_COORD(drop_off_coordinates.x, drop_off_coordinates.y, drop_off_coordinates.z);
			if (use_default_blip == 0) UI::SET_BLIP_SPRITE(drop_off_blip_, 50);
			else UI::SET_BLIP_SPRITE(drop_off_blip_, 1);
			UI::SET_BLIP_COLOUR(drop_off_blip_, 5);
			UI::SET_BLIP_DISPLAY(drop_off_blip_, (char)"you will never see this");
			CreateNotification("The ~y~vehicle~w~ is ready to be delivered.", play_notification_beeps);
			mission_objective_ = DeliverCar;
		}
	}

	if (mission_objective_ == FixCar) {
		if (!VEHICLE::_IS_VEHICLE_DAMAGED(vehicle_to_steal_)) {
			CreateNotification("The ~y~vehicle~w~ is ready to be delivered.", play_notification_beeps);
			mission_objective_ = DeliverCar;
		}
	}

	if (mission_objective_ == DeliverCar) {
		if (vehicle_must_be_undamaged_ && VEHICLE::_IS_VEHICLE_DAMAGED(vehicle_to_steal_)) {
			CreateNotification("Repair the ~y~vehicle~w~ before making delivery.", play_notification_beeps);
			mission_objective_ = FixCar;
		}
		GRAPHICS::DRAW_MARKER(1, drop_off_coordinates.x, drop_off_coordinates.y, drop_off_coordinates.z, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 5.0f, 5.0f, 1.0f, 204, 204, 0, 100, false, false, 2, false, false, false, false); // redraws every frame, no need to remove later
		current_mission_objective = Vector4{ drop_off_coordinates, 0.0f };
		Vector3 vehicle_coordinates = ENTITY::GET_ENTITY_COORDS(vehicle_to_steal_, 0);
		float dropOffDistance = GAMEPLAY::GET_DISTANCE_BETWEEN_COORDS(vehicle_coordinates.x, vehicle_coordinates.y, vehicle_coordinates.z, drop_off_coordinates.x, drop_off_coordinates.y, drop_off_coordinates.z, 0);
		if (dropOffDistance < 0.7f && PED::IS_PED_IN_VEHICLE(player_ped, vehicle_to_steal_, 0)) {
			VEHICLE::SET_VEHICLE_FORWARD_SPEED(vehicle_to_steal_, 0.0f);
			while (ENTITY::GET_ENTITY_SPEED(vehicle_to_steal_) != 0) Wait(0);
			AI::TASK_LEAVE_VEHICLE(player_ped, vehicle_to_steal_, 0);
			VEHICLE::SET_VEHICLE_UNDRIVEABLE(vehicle_to_steal_, 1);
			int reward_amount_by_class;
			switch (GetVehicleClassBitwiseFromHash(vehicle_hash_)) {
			case Compact: reward_amount_by_class = 1969; break; // derived from a combination of SP and Online values for Legendary Motorsports
			case Sedan : reward_amount_by_class = 12200; break; // and Los Santos Customs. Probably not very accurate...
			case SUV : reward_amount_by_class = 13578; break;
			case Coupe : reward_amount_by_class = 20646; break;
			case Muscle : reward_amount_by_class = 14036; break;
			case SportsClassic : reward_amount_by_class = 92113; break;
			case Sports : reward_amount_by_class = 26555; break;
			case Super : reward_amount_by_class = 121232; break;
			case Motorcycle : reward_amount_by_class = 34171; break;
			case OffRoad : reward_amount_by_class = 10750; break;
			//case Industrial : reward_amount_by_class = 0; break;
			case Utility : reward_amount_by_class = 3500; break;
			case Van : reward_amount_by_class = 1886; break;
			//case Cycle : reward_amount_by_class = 0; break;
			//case Boat : reward_amount_by_class = 0; break;
			//case Helicopter : reward_amount_by_class = 0; break;
			//case Plane : reward_amount_by_class = 0; break;
			//case Service : reward_amount_by_class = 0; break;
			//case Emergency : reward_amount_by_class = 0; break;
			//case Military : reward_amount_by_class = 0; break;
			//case Commercial : reward_amount_by_class = 0; break;
			//case Train : reward_amount_by_class = 0; break;
			default: reward_amount_by_class = 29386; break; // until I define values for non-default classes, this average of all values will fill in.
			}
			float reward_modifier = GetFromUniformRealDistribution(-0.5f, 1.5f); // you lose some, you win some more.
			ChangeMoneyForCurrentPlayer(int(reward_amount_by_class * reward_modifier), mission_reward_modifier); // who cares about rounding errors?
			CreateNotification("The ~y~vehicle~w~ has been delivered.", play_notification_beeps);
			SafelyRemoveBlip( &drop_off_blip_ );
			SafelyCleanupEntity( &vehicle_to_steal_ );
			current_mission_objective = Vector4{ 999,999,999,999 };
			return NO_Mission;
		}
	}
	return StealVehicle;
}

MissionType StealVehicleMission::Timeout() {
	Vector3 vehicle_coordinates = ENTITY::GET_ENTITY_COORDS(vehicle_to_steal_, 0);
	uint vehicleDistance = GAMEPLAY::GET_DISTANCE_BETWEEN_COORDS(vehicle_coordinates.x, vehicle_coordinates.y, vehicle_coordinates.z, player_position.x, player_position.y, player_position.z, 0);
	if (vehicleDistance > mission_minimum_range_for_timeout || ENTITY::IS_ENTITY_DEAD(player_ped)) {
		CreateNotification("The ~y~special vehicle~w~ is no longer requested.", play_notification_beeps);
		if ( mission_objective_ == DeliverCar ) SafelyRemoveBlip( &drop_off_blip_ );
		SafelyCleanupEntity( &vehicle_to_steal_ );
		current_mission_objective = Vector4{ 999,999,999,999 };
		return NO_Mission;
	}
	return StealVehicle;
}

class SelfDefenseMission {
public:
	MissionType Prepare();
	MissionType Execute();
	MissionType Timeout();
private:
	Ped assassin_ped_;
	Blip assassin_blip_;
};

MissionType SelfDefenseMission::Prepare() {
	Logger.Write( "SelfDefenseMission::Prepare()", LogNormal );
	Vector4 assassin_spawn_position = SelectASpawnPoint( player_position, vehicle_spawn_points, reserved_vehicle_spawn_points, spawn_point_maximum_range, spawn_point_minimum_range, NULL );
	if ( assassin_spawn_position.x == 0.0f && assassin_spawn_position.y == 0.0f && assassin_spawn_position.z == 0.0f && assassin_spawn_position.h == 0.0f ) return NO_Mission;
	assassin_ped_ = PED::CREATE_RANDOM_PED( assassin_spawn_position.x, assassin_spawn_position.y, assassin_spawn_position.z );
	while ( !ENTITY::DOES_ENTITY_EXIST( assassin_ped_ ) ) Wait( 0 );
	PED::SET_PED_DESIRED_HEADING( assassin_ped_, assassin_spawn_position.h );
	assassin_blip_ = UI::ADD_BLIP_FOR_ENTITY( assassin_ped_ );
	AI::TASK_WANDER_STANDARD( assassin_ped_, 1000.0f, 0 );
	if ( use_default_blip ) UI::SET_BLIP_SPRITE( assassin_blip_, 1 );
	else UI::SET_BLIP_SPRITE( assassin_blip_, 432 );
	UI::SET_BLIP_COLOUR( assassin_blip_, 1 );
	UI::SET_BLIP_DISPLAY( assassin_blip_, ( char )"you will never see this" );
	CreateNotification( "A hit has been placed on a ~r~target~w~.", play_notification_beeps );
	return Assassination;
}

MissionType SelfDefenseMission::Execute() {
	if ( ENTITY::IS_ENTITY_DEAD( assassin_ped_ ) ) {
		SafelyCleanupEntity( &assassin_ped_ );
		ChangeMoneyForCurrentPlayer( GetFromUniformIntDistribution( 5, 25 ) * 1000, mission_reward_modifier );
		CreateNotification( "The ~r~target~w~ has been eliminated.", play_notification_beeps );
		return NO_Mission;
	}
	return Assassination;
}

MissionType SelfDefenseMission::Timeout() {
	Vector3 target_coordinates = ENTITY::GET_ENTITY_COORDS( assassin_ped_, 0 );
	uint distance_to_target = GAMEPLAY::GET_DISTANCE_BETWEEN_COORDS( target_coordinates.x, target_coordinates.y, target_coordinates.z, player_position.x, player_position.y, player_position.z, 0 );
	if ( distance_to_target > mission_minimum_range_for_timeout || ENTITY::IS_ENTITY_DEAD( player_ped ) ) {
		CreateNotification( "The contract on the ~r~target~w~ has expired.", play_notification_beeps );
		SafelyCleanupEntity( &assassin_ped_ );
		return NO_Mission;
	}
	return Assassination;
}

class BadGuyHandler {
public:
	Ped CreateABadGuy(Vector4 origin_vector = player_position, char * skin = "mp_g_m_pros_01", float x_margin = 1.0, float y_margin = 1.0, float z_margin = 0.0, char * weapon = "SURPRISE_ME");
	bool RemoveABadGuy(Ped bad_guy);
	std::set<Ped> CreateABadGuySquad(Vector4 origin_vector = player_position, char * skin = "mp_g_m_pros_01", float x_margin = 1.0, float y_margin = 1.0, float z_margin = 0.0, char * weapon = "SURPRISE_ME", uint quantity_of_bad_guys = 1);
	bool RemoveABadGuySquad(std::set<Ped> squad);
private:
	int default_armor_ = 100;
	int default_accuracy_ = 18;
	std::set<Ped> bad_guys_;
	std::set<std::set<Ped>> bad_guy_squads_;
};

Ped BadGuyHandler::CreateABadGuy(Vector4 origin_vector, char * skin, float x_margin, float y_margin, float z_margin, char * weapon)
{
	Logger.Write("CreateABadGuy()", LogDebug);
	Vector4 spawn_point = origin_vector;
	origin_vector.x += GetFromUniformRealDistribution(-x_margin, x_margin); origin_vector.y += GetFromUniformRealDistribution(-y_margin, y_margin); origin_vector.z += GetFromUniformRealDistribution(-z_margin, z_margin);
	Ped skin_hash_key = GAMEPLAY::GET_HASH_KEY(skin);
	STREAMING::REQUEST_MODEL(skin_hash_key);
	Ped this_ped = PED::CREATE_PED(26, skin_hash_key, origin_vector.x, origin_vector.y, origin_vector.z, 0, false, true);
	while (!ENTITY::DOES_ENTITY_EXIST(this_ped)) Wait(0);
	PED::SET_PED_RELATIONSHIP_GROUP_HASH(this_ped, GAMEPLAY::GET_HASH_KEY("HATES_PLAYER"));
	if (weapon != "NONE") {
		if (weapon == "SURPRISE_ME") {
			if (GetFromUniformRealDistribution(0, 1) <= (0.01)) weapon = "WEAPON_RAILGUN";
			else if (GetFromUniformRealDistribution(0, 1) <= (0.04)) weapon = "WEAPON_RPG";
			else if (GetFromUniformRealDistribution(0, 1) <= (0.04)) weapon = "WEAPON_GRENADELAUNCHER";
			else if (GetFromUniformRealDistribution(0, 1) <= (0.04)) weapon = "WEAPON_MG";
			else if (GetFromUniformRealDistribution(0, 1) <= (0.12)) weapon = "WEAPON_SNIPERRIFLE";
			else if (GetFromUniformRealDistribution(0, 1) <= (0.24)) weapon = "WEAPON_CARBINERIFLE";
			else if (GetFromUniformRealDistribution(0, 1) <= (0.33)) weapon = "WEAPON_SMG";
			else if (GetFromUniformRealDistribution(0, 1) <= (0.33)) weapon = "WEAPON_PUMPSHOTGUN";
			else if (GetFromUniformRealDistribution(0, 1) <= (0.24)) weapon = "WEAPON_PISTOL50";
			else if (GetFromUniformRealDistribution(0, 1) <= (0.24)) weapon = "WEAPON_COMBATPISTOL";
			else weapon = "WEAPON_PISTOL";
		}
		try {
			WEAPON::GIVE_DELAYED_WEAPON_TO_PED(this_ped, GAMEPLAY::GET_HASH_KEY((char *)weapon), 1000, 1);
			WEAPON::SET_CURRENT_PED_WEAPON(this_ped, GAMEPLAY::GET_HASH_KEY((char *)weapon), 1);
		}
		catch (...) {
			Logger.Write("SpawnAnEnemyPlayer(): exception adding a weapon! Probably the string was not a valid weapon!", LogError);
		}
		
	}
	PED::SET_PED_ARMOUR(this_ped, default_armor_);
	PED::SET_PED_ACCURACY(this_ped, default_accuracy_);
	ENTITY::SET_ENTITY_INVINCIBLE(this_ped, false);
	bad_guys_.insert(this_ped);
	return this_ped;
}

bool BadGuyHandler::RemoveABadGuy(Ped bad_guy)
{
	if (bad_guys_.find(bad_guy) != bad_guys_.end()) {
		SafelyCleanupEntity( &bad_guy );
		bad_guys_.erase(bad_guy);
		return true;
	}
	return false;
}

std::set<Ped> BadGuyHandler::CreateABadGuySquad(Vector4 origin_vector, char * skin, float x_margin, float y_margin, float z_margin, char * weapon, uint quantity_of_bad_guys) {
	std::set<Ped> this_squad;
	Logger.Write("CreateABadGuySquad()", LogDebug);
	for (uint i = 0; i < quantity_of_bad_guys; i++) {
		this_squad.insert(CreateABadGuy(origin_vector, skin, (x_margin + quantity_of_bad_guys), (y_margin + quantity_of_bad_guys), (z_margin + quantity_of_bad_guys), weapon));
	}
	bad_guy_squads_.insert(this_squad);
	return this_squad;
}

bool BadGuyHandler::RemoveABadGuySquad(std::set<Ped> squad)
{
	if (bad_guy_squads_.find(squad) != bad_guy_squads_.end()) {
		for (Ped each_bad_guy : squad) {
			SafelyCleanupEntity( &each_bad_guy );
		}
		bad_guy_squads_.erase(squad);
		return true;
	}
	return false;
}

class MissionHandler {
public:
	void Update();
private:
	MissionType current_mission_type_ = NO_Mission;
	ULONGLONG tick_count_at_last_update_ = GetTickCount64();
	ULONGLONG ticks_since_last_mission_ = 0;
	ULONGLONG ticks_between_missions_ = mission_cooldown * 1000;
	ULONGLONG ticks_since_mission_start_ = 0;
	ULONGLONG ticks_until_timeout_ = mission_timeout * 1000;
	StealVehicleMission StealVehicleMission;
	DestroyVehicleMission DestroyVehicleMission;
	AssassinationMission AssassinationMission;
	ArmoredTruckMission ArmoredTruckMission;
	CrateDropMission CrateDropMission;
};

void MissionHandler::Update() {
	if (current_mission_type_ == NO_Mission) ticks_since_last_mission_ += (GetTickCount64() - tick_count_at_last_update_);
	else ticks_since_mission_start_ += (GetTickCount64() - tick_count_at_last_update_);
	tick_count_at_last_update_ = GetTickCount64();
	// Enough time has passed that we can start a new mission.
	if (ticks_since_last_mission_ > ticks_between_missions_) { 
		//current_mission_type_ = MissionType(rand() % MAX_Mission); // I used to think this was silly. Now I think it's awesome.
		current_mission_type_ = CrateDrop;
		//current_mission_type_ = ArmoredTruck;
		switch (current_mission_type_) {
		case StealVehicle:	current_mission_type_ = StealVehicleMission.Prepare();		break; // Prepare()s should return their MissionType on success.
		case DestroyVehicle:	current_mission_type_ = DestroyVehicleMission.Prepare();	break; // If something goes wrong (StealVehicleMission takes too
		case Assassination:	current_mission_type_ = AssassinationMission.Prepare();		break; // long to find a vehicle), they can return NO_Mission
		case ArmoredTruck:	current_mission_type_ = ArmoredTruckMission.Prepare();		break;
		case CrateDrop:	current_mission_type_ = CrateDropMission.Prepare();			break;
		}
		if (current_mission_type_ != NO_Mission) {
			ticks_since_last_mission_ = 0;  // Clear the timer since the last mission. This won't increment again until current_mission_type_ equals NO_Mission.
			ticks_since_mission_start_ = 0; // initialize the timeout delay as well, since it's about to begin incrementing.
		}
	}

	switch (current_mission_type_) {
	case StealVehicle:	current_mission_type_ = StealVehicleMission.Execute();		break; // if MissionType matches, the subroutine will execute.
	case DestroyVehicle:	current_mission_type_ = DestroyVehicleMission.Execute();	break; // Each Execute will be responsible for returning its
	case Assassination:	current_mission_type_ = AssassinationMission.Execute();		break; // own type while active, and returning NO_Mission
	case ArmoredTruck:	current_mission_type_ = ArmoredTruckMission.Execute();		break; // when finished.
	case CrateDrop:	current_mission_type_ = CrateDropMission.Execute();			break;
	}

	// Timeout() asks the mission to timeout, but if the player hasn't met certain criteria (like a minimum range), 
	// the mission may allow them to continue. So the Handler needs to wait until the Mission returns NO_Mission.

	if (ticks_since_mission_start_ > ticks_until_timeout_) {
		switch (current_mission_type_) {
		case StealVehicle:	current_mission_type_ = StealVehicleMission.Timeout();		break;
		case DestroyVehicle:	current_mission_type_ = DestroyVehicleMission.Timeout();	break;
		case Assassination:	current_mission_type_ = AssassinationMission.Timeout();		break;
		case ArmoredTruck:	current_mission_type_ = ArmoredTruckMission.Timeout();		break;
		case CrateDrop:	current_mission_type_ = CrateDropMission.Timeout();			break;
		}
	}
}

bool ToggleCrateSpawnBlips(bool current_state) {
	if (!current_state) { // currently false - meaning blips are off
		CreateNotification("Displaying blips for crates", play_notification_beeps);
		for (Vector4 vec : crate_spawn_points) {
			Blip this_blip = UI::ADD_BLIP_FOR_COORD(vec.x, vec.y, vec.z);
			crate_spawn_blips.insert(this_blip);
			UI::SET_BLIP_COLOUR(this_blip, 1);
		}
	}
	if (current_state) { // blips are on
		CreateNotification("Removing blips for crates", play_notification_beeps);
		for (Blip this_blip : crate_spawn_blips) {
			SafelyRemoveBlip(&this_blip);
			crate_spawn_blips.erase(this_blip);
		}
	}
	return !current_state;
}

void InputHandler() {
	if (IsControlPressed(ControlScriptPadDown) && IsControlJustReleased(ControlScriptRS)) {
		Logger.Write("InputHandler(): (IsControlPressed(ControlScriptPadDown) && IsControlJustReleased(ControlScriptRS))", LogVerbose);
		
		if ( debug_enable ) {
			CreateNotification( "saving point to file", play_notification_beeps );
			crate_spawn_points.push_back( player_position );
			crate_spawn_file << std::setprecision( 9 ) << player_position.x << " , " << player_position.y << " , " << player_position.z << std::endl;
		}

		//CreateNotification("WITH COLLISION", play_notification_beeps);
		//crate_hash = GAMEPLAY::GET_HASH_KEY("prop_box_ammo04a");
		//STREAMING::REQUEST_MODEL(GAMEPLAY::GET_HASH_KEY("prop_box_ammo04a"));
		//while (!STREAMING::HAS_MODEL_LOADED(GAMEPLAY::GET_HASH_KEY("prop_box_ammo04a"))) Wait(0);
		//Vector3 v3 = GetCoordinateByOriginBearingAndDistance(player_position, 0.01f, 3.0f);
		//crate = OBJECT::CREATE_AMBIENT_PICKUP(0x14568F28, v3.x, v3.y, v3.z+2, -1, 0, crate_hash, 1, 1);
		//ENTITY::SET_ENTITY_ALPHA(crate, 255, 1);
		//STREAMING::REQUEST_COLLISION_FOR_MODEL(crate_hash);
		//while (!STREAMING::HAS_COLLISION_FOR_MODEL_LOADED(crate_hash)) Wait(0);
		//ENTITY::SET_ENTITY_COLLISION(crate, true, true); 
		//crate_blip = UI::ADD_BLIP_FOR_ENTITY(crate);

		//float ground_z = GetGroundZAtThisLocation(player_position);
		//float my_calculated_height_above_ground = player_position.z - ground_z;
		//std::stringstream ss;
		//ss << std::fixed << std::setprecision(3) << "InputHandler() : player_position: ( " << player_position.x << ", " << player_position.y << ", " << player_position.z << " ) Ground Z: " << ground_z;
		//Logger.Write(ss.str(), LogNormal);
		//std::stringstream ss2;
		//ss2 << std::fixed << std::setprecision(3) << "ENTITY::GET_ENTITY_HEIGHT_ABOVE_GROUND(player_position): " << ENTITY::GET_ENTITY_HEIGHT_ABOVE_GROUND(player_ped);
		//Logger.Write(ss2.str(), LogNormal);
		//std::stringstream ss3;
		//ss3 << std::fixed << std::setprecision(3) << "my_calculated_height: " << my_calculated_height_above_ground;
		//Logger.Write(ss3.str(), LogNormal);
		
	}
	if (IsControlPressed(ControlScriptPadDown) && IsControlJustReleased(ControlScriptRB)) {
		if ( debug_enable ) crate_spawn_blips_current_state = ToggleCrateSpawnBlips(crate_spawn_blips_current_state);

		//CreateNotification("WITHOUT COLLISION", play_notification_beeps);
		//crate_hash = GAMEPLAY::GET_HASH_KEY("prop_box_ammo04a");
		//STREAMING::REQUEST_MODEL(GAMEPLAY::GET_HASH_KEY("prop_box_ammo04a"));
		//while (!STREAMING::HAS_MODEL_LOADED(GAMEPLAY::GET_HASH_KEY("prop_box_ammo04a"))) Wait(0);
		//Vector3 v3 = GetCoordinateByOriginBearingAndDistance(player_position, 180.01f, 3.0f);
		//crate = OBJECT::CREATE_AMBIENT_PICKUP(0x14568F28, v3.x, v3.y, v3.z+2, -1, 0, crate_hash, 1, 1);
		//ENTITY::SET_ENTITY_ALPHA(crate, 255, 1);
		//crate_blip = UI::ADD_BLIP_FOR_ENTITY(crate);

		/*STREAMING::REQUEST_COLLISION_FOR_MODEL(crate_hash);
		while (!STREAMING::HAS_COLLISION_FOR_MODEL_LOADED(crate_hash)) Wait(0);
		ENTITY::SET_ENTITY_COLLISION(crate, true, true);*/
	}
	if (IsKeyDown(VK_OEM_4) && IsKeyJustUp(VK_OEM_6)) { // open then close
		//CreateNotification("Displaying blips for crates", play_notification_beeps);
		//for (Vector4 vec : crate_spawn_points) {
		//	Blip this_blip = UI::ADD_BLIP_FOR_COORD(vec.x, vec.y, vec.z);
		//	crate_spawn_blips.insert(this_blip);
		//	UI::SET_BLIP_COLOUR(this_blip, 1);
		//}
	}
	if (IsKeyDown(VK_OEM_6) && IsKeyJustUp(VK_OEM_4)) { // close then open
		//CreateNotification("Removing blips for crates", play_notification_beeps);
		//for (Blip this_blip : crate_spawn_blips) {
		//	SafelyRemoveBlip(&this_blip);
		//	crate_spawn_blips.erase(this_blip);
		//}
	}
	if (IsControlJustReleased(ControlSprint)) {
		

		//std::stringstream ss;
		//ss << std::setprecision(3) << "2DAngle: " << angle << " Heading: " << heading << " Bearing: " << bearing;
		//std::string str = ss.str();
		//char* chr = &str[0u];
		//NotifyBottomCenter(chr, 2222);
	}
}

void Update() {
	player_ped = PLAYER::PLAYER_PED_ID();
	player = PLAYER::PLAYER_ID(); // need to be updated every cycle?
	player_position = Vector4(ENTITY::GET_ENTITY_COORDS(player_ped, 0), ENTITY::GET_ENTITY_HEADING(player_ped));
	vehicle_spawn_points = GetParkedVehiclesFromWorld(player_ped, vehicle_spawn_points, maximum_number_of_spawn_points, vehicle_search_range_minimum);
	possible_vehicle_models = GetVehicleModelsFromWorld(player_ped, possible_vehicle_models, maximum_number_of_vehicle_models);
	InputHandler();
	if (debug_enable && distance_to_draw_debug_markers > 0) {
		for (Vector4 v4 : vehicle_spawn_points) {
			if (GetDistanceBetween2DCoords(player_position.x, player_position.y, v4.x, v4.y) < distance_to_draw_debug_markers) {
				GRAPHICS::DRAW_MARKER(1, v4.x, v4.y, v4.z, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 300.0f, 0, 0, 255, 192, false, false, 2, false, false, false, false); // redraws every frame, no need to remove later
			}
		}
		for (Vector4 v4 : crate_spawn_points) {
			if (GetDistanceBetween2DCoords(player_position.x, player_position.y, v4.x, v4.y) < distance_to_draw_debug_markers) {
				GRAPHICS::DRAW_MARKER(1, v4.x, v4.y, v4.z, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 300.0f, 0, 255, 0, 192, false, false, 2, false, false, false, false); // redraws every frame, no need to remove later
			}
		}
		for (Vector4 v4 : special_marker_points) {
			if (GetDistanceBetween2DCoords(player_position.x, player_position.y, v4.x, v4.y) < distance_to_draw_debug_markers) {
				GRAPHICS::DRAW_MARKER(1, v4.x, v4.y, v4.z, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 300.0f, 255, 0, 255, 192, false, false, 2, false, false, false, false); // redraws every frame, no need to remove later
			}
		}
		if (GetDistanceBetween2DCoords(player_position, current_mission_objective) < distance_to_draw_debug_markers) {
			GRAPHICS::DRAW_MARKER(1, current_mission_objective.x, current_mission_objective.y, current_mission_objective.z, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.5f, 1.5f, 300.0f, 255, 255, 0, 192, false, false, 2, false, false, false, false); // redraws every frame, no need to remove later
		}
		GRAPHICS::DRAW_MARKER(1, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.5f, 1.5f, 300.0f, 255, 255, 255, 192, false, false, 2, false, false, false, false);
	}
}

void WaitDuringDeathArrestOrLoading(uint milliseconds) {
	if (ENTITY::IS_ENTITY_DEAD(PLAYER::PLAYER_PED_ID())) {
		Logger.Write("WaitDuringDeathArrestOrLoading(): Player is Wasted, waiting for player...", LogNormal);
		while (ENTITY::IS_ENTITY_DEAD(PLAYER::PLAYER_PED_ID())) Wait(milliseconds);
		Logger.Write("WaitDuringDeathArrestOrLoading(): Returning to game.", LogNormal);
	}
	if (PLAYER::IS_PLAYER_BEING_ARRESTED(PLAYER::PLAYER_ID(), TRUE)) {
		Logger.Write("WaitDuringDeathArrestOrLoading(): Player is Busted, waiting for player...", LogNormal);
		while (PLAYER::IS_PLAYER_BEING_ARRESTED(PLAYER::PLAYER_ID(), TRUE)) Wait(milliseconds);
		Logger.Write("WaitDuringDeathArrestOrLoading(): Returning to game.", LogNormal);

	}
	if (DLC2::GET_IS_LOADING_SCREEN_ACTIVE()) {
		Logger.Write("WaitDuringDeathArrestOrLoading(): Loading screen is active, waiting for game...", LogNormal);
		while (DLC2::GET_IS_LOADING_SCREEN_ACTIVE()) Wait(milliseconds);
		Logger.Write("WaitDuringDeathArrestOrLoading(): Returning to game.", LogNormal);
	}
	return;
}

void UglyHackForVehiclePersistenceScripts(uint seconds) {
	Logger.Write("UglyHackForVehiclePersistenceScripts()", LogNormal);
	if ((seconds == 0)) return; // no point in wasting time or spawns if the user doesn't use a persistence script.
	Wait(DWORD(seconds * 1000)); // let the persistence script start up and spawn its own vehicles.
	reserved_vehicle_spawn_points = GetParkedVehiclesFromWorld(player_ped, reserved_vehicle_spawn_points, maximum_number_of_spawn_points, vehicle_search_range_minimum); // set aside these spawns. Note that some legitimate spawns will invariably get reserved. This is why it's an ugly hack.
	Logger.Write("reserved_vehicle_spawn_points_parked.size(): " + std::to_string(reserved_vehicle_spawn_points.size()), LogNormal);
	return;
}

void GetSettingsFromIniFile() {
	Logger.Write("GetSettingsFromIniFile()", LogNormal);
	// OPTIONS
	load_without_notification = Reader.ReadBoolean( "Options", "load_without_notification", false );
	play_notification_beeps = Reader.ReadBoolean("Options", "play_notification_beeps", true);
	use_default_blip = Reader.ReadBoolean("Options", "use_default_blip", false);
	mission_timeout = std::max(Reader.ReadInteger("Options", "mission_timeout", 360), 180);
	mission_cooldown = std::max(Reader.ReadInteger("Options", "mission_cooldown", 60), 5);
	spawn_point_minimum_range = Reader.ReadInteger("Options", "spawn_point_minimum_range", 1111);
	spawn_point_maximum_range = Reader.ReadInteger("Options", "spawn_point_maximum_range", 3333);
	mission_minimum_range_for_timeout = Reader.ReadInteger("Options", "mission_minimum_range_for_timeout", 333);
	mission_reward_modifier = Reader.ReadFloat("Options", "mission_reward_modifier", 1.0f);
	destroyable_vehicle_classes = Reader.ReadInteger("Options", "destroyable_vehicle_flags", SUV | Muscle | OffRoad | Motorcycle);
	stealable_vehicle_classes = Reader.ReadInteger("Options", "stealable_vehicle_flags", Compact | Sedan | SUV | Coupe | Muscle | SportsClassic | Sports | Super | Motorcycle | OffRoad);
	number_of_guards_to_spawn = std::min(Reader.ReadInteger("Options", "number_of_guards_to_spawn", 4), 12);
	time_between_guard_respawns = std::max(std::min( Reader.ReadInteger( "Options", "time_between_guard_respawns", 3333 ), 9999 ), 666); // between 666 and 9999 milliseconds

	// DEBUG
	logging_level = LogLevel (std::max(Reader.ReadInteger("Debug", "logging_level", 1), 1)); // right now at least, I don't want to let anyone turn logging entirely off.
	debug_enable = Reader.ReadBoolean( "Debug", "debug_enable", false );
	seconds_to_wait_for_vehicle_persistence_scripts = Reader.ReadInteger("Debug", "seconds_to_wait_for_vehicle_persistence_scripts", 0);
	vehicle_search_range_minimum = Reader.ReadInteger("Debug", "vehicle_search_range_minimum", 30);
	maximum_number_of_spawn_points = Reader.ReadInteger("Debug", "maximum_number_of_spawn_points", 1000000);
	maximum_number_of_vehicle_models = Reader.ReadInteger("Debug", "maximum_number_of_vehicle_models", 1000);
	distance_to_draw_debug_markers = std::max(Reader.ReadInteger("Debug", "distance_to_draw_spawn_points", 0), 0);
	dump_parked_cars_to_xyz_file = Reader.ReadBoolean("Debug", "dump_parked_cars_to_xyz_file", false);
	return;
}

void Init() {
	Logger.Write("init()", LogNormal);
	srand(GetTickCount64());
	GetSettingsFromIniFile();
	Logger.SetLoggingLevel(logging_level);
	WaitDuringDeathArrestOrLoading(3333);
	UglyHackForVehiclePersistenceScripts(seconds_to_wait_for_vehicle_persistence_scripts); // UGLY HACK FOR VEHICLE PERSISTENCE!
	if (debug_enable && dump_parked_cars_to_xyz_file) {
		std::ifstream file_exists(".\\OnlineEventsRedux.xyz");
    if (!file_exists) {
      xyz_file.open(".\\OnlineEventsRedux.xyz", std::fstream::in | std::fstream::out | std::fstream::app);
      xyz_file << "name,latitude,longitude,altitude,heading" << std::endl;
    }
		else {
			xyz_file.open(".\\OnlineEventsRedux.xyz", std::fstream::in | std::fstream::out | std::fstream::app);
		}
	}
	crate_spawn_file.open(".\\crate_spawn_file.xyz", std::fstream::in | std::fstream::out | std::fstream::app);
}

void ScriptMain() {
	Logger.Write("ScriptMain()", LogNormal);

	if (FileExists("OnlineEventsReduxRedux.asi")) {
		Logger.Write("OnlineEventsReduxRedux.asi - FILE EXISTS, MOVING TO PASSIVE LOOP", LogError);
		WaitDuringDeathArrestOrLoading(3333);
		ULONGLONG tick_start = GetTickCount64();
		ULONGLONG ticks_to_wait = 10000;
		CreateNotification("~b~Online Events Redux!~w~ (v1.1.2)", play_notification_beeps);
		while (true) {
			ULONGLONG current_tick = GetTickCount64();
			if (current_tick - tick_start > ticks_to_wait) {
				CreateNotification("~r~WARNING:~n~~w~A new ~y~conflicting~w~ version of~n~Online Events has been released, and you are", false);
				CreateNotification("still running the old version!", false);
				CreateNotification("~r~PLEASE REMOVE:~n~~b~OnlineEventsRedux~y~Redux~b~.asi~w~~n~and restart your game!", false);
				tick_start = GetTickCount64();
				ticks_to_wait += 90000;
			}
			Wait(0);
		}
	}

	Init();
	if ( !load_without_notification ) CreateNotification("~b~Online Events Redux!~w~ (v1.1.2)", play_notification_beeps);
	MissionHandler MissionHandler;

	std::vector<double> profiles_count;


	while (true) {
		WAIT(0);
		std::chrono::high_resolution_clock::time_point loop_start = std::chrono::high_resolution_clock::now();
		WaitDuringDeathArrestOrLoading(0);
		Update();
		MissionHandler.Update();
		if (profiles_count.size() == 2048) {
			double average = std::accumulate(profiles_count.begin(), profiles_count.end(), 0.0) / profiles_count.size();
			std::string profiler_string = "ScriptMain(): profiler_duration (last " + std::to_string(profiles_count.size()) + "):  " + std::to_string(average) + "  times_waited: " + std::to_string(times_waited);
			Logger.Write(profiler_string, LogDebug);
			if ( debug_enable ) NotifyBottomCenter(&profiler_string[0], 6666);
			times_waited = 0;
			profiles_count.clear();
		}
		profiles_count.push_back((std::chrono::duration_cast<std::chrono::duration<double>>(std::chrono::high_resolution_clock::now() - loop_start)).count() * 1000);
	}

	Logger.Close(); // I don't think this will ever happen...
}



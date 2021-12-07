#include "fastestlapc.h"
#include<iostream>
#include<unordered_map>

#include "src/core/vehicles/lot2016kart.h"
#include "src/core/applications/steady_state.h"
#include "src/core/applications/optimal_laptime.h"

std::unordered_map<std::string,lot2016kart_all> vehicles_lot2016kart;
std::unordered_map<std::string,Track_by_arcs> tracks_by_arcs;

void create_vehicle(struct c_Vehicle* vehicle, const char* name, const char* database_file)
{
    const std::string s_database = database_file;

    // Copy the vehicle name
    vehicle->name = new char[strlen(name)+1];
    memcpy(vehicle->name, name, strlen(name));
    vehicle->name[strlen(name)] = '\0';
 
    // Copy the database file path
    vehicle->database_file = new char[strlen(database_file)+1];
    memcpy(vehicle->database_file, database_file, strlen(database_file));
    vehicle->database_file[strlen(database_file)] = '\0';

    // Open the database as Xml
    Xml_document database = { database_file, true }; 

    // Get vehicle type from the database file
    const std::string vehicle_type = database.get_root_element().get_attribute("type");

    if ( vehicle_type == "roberto-lot-kart-2016" )
    {
        vehicle->type = LOT2016KART;
        auto out = vehicles_lot2016kart.insert({name,{database}});
        if (out.second==false) 
        {
            throw std::runtime_error("Vehicle already exists");
        }
    }
    else
    {
        throw std::runtime_error("Vehicle type not recognized");
    }
}

void create_track(struct c_Track* track, const char* name, const char* track_file, const double scale)
{
    const std::string s_track_file = track_file;

    // Copy the track name
    track->name = new char[strlen(name)+1];
    memcpy(track->name, name, strlen(name));
    track->name[strlen(name)] = '\0';
 
    // Copy the track file path
    track->track_file = new char[strlen(track_file)+1];
    memcpy(track->track_file, track_file, strlen(track_file));
    track->track_file[strlen(track_file)] = '\0';

    // Copy the track scale
    track->scale = scale;

    // Open the track as Xml
    Xml_document track_xml = { track_file, true }; 

    // Read type: open or closed
    const std::string track_type = track_xml.get_root_element().get_attribute("type");
    bool is_closed;

    if ( track_type == "closed" )
        is_closed = true;

    else if ( track_type == "open" )
        is_closed = false;

    else
        throw std::runtime_error("Track attribute type \"" + track_type + "\" shall be \"open\" or \"closed\"");

    track->is_closed = is_closed;
    
    auto out = tracks_by_arcs.insert({name,{track_xml,scale,is_closed}});

    if (out.second==false) 
    {
        throw std::runtime_error("Track already exists");
    }
}



void gg_diagram(double* ay, double* ax_max, double* ax_min, struct c_Vehicle* vehicle, double v, const int n_points)
{
    auto& car = vehicles_lot2016kart.at(vehicle->name).cartesian_ad;
    Steady_state ss(car);
    auto [sol_max, sol_min] = ss.gg_diagram(v,n_points);

    for (int i = 0; i < n_points; ++i)
    {
        ay[i] = sol_max[i].ay;
        ax_max[i] = sol_max[i].ax;
        ax_min[i] = sol_min[i].ax;
    }
}


void track_coordinates(double* x_center, double* y_center, double* x_left, double* y_left, double* x_right, double* y_right, struct c_Track* c_track, const double width, const int n_points)
{
    auto& track = tracks_by_arcs.at(c_track->name);

    const scalar& L = track.get_total_length();
    const scalar ds = L/((scalar)(n_points-1));
    
    for (int i = 0; i < n_points; ++i)
    {
        const scalar s = ((double)i)*ds;

        // Compute centerline
        auto [r_c,v_c,a_c] = track(s);

        x_center[i] = r_c[0];
        y_center[i] = r_c[1];

        // Compute left boundary
        auto [r_l,v_l,a_l] = track.position_at(s,width,0.0,0.0);

        x_left[i] = r_l[0];
        y_left[i] = r_l[1];

        // Compute right boundary
        auto [r_r,v_r,a_r] = track.position_at(s,-width,0.0,0.0);

        x_right[i] = r_r[0];
        y_right[i] = r_r[1];
    }

    return;
}


void optimal_laptime(double* x, double* y, double* delta, double* T, struct c_Vehicle* c_vehicle, const c_Track* c_track, const double width, const int n_points)
{
    auto& track = tracks_by_arcs.at(c_track->name);
    auto& car_curv = vehicles_lot2016kart.at(c_vehicle->name).curvilinear_ad;
    auto& car_cart = vehicles_lot2016kart.at(c_vehicle->name).cartesian_ad;
    auto& car_cart_sc = vehicles_lot2016kart.at(c_vehicle->name).cartesian_scalar;
    auto& car_curv_sc = vehicles_lot2016kart.at(c_vehicle->name).curvilinear_scalar;

    // Set the track into the curvilinear car dynamic model
    car_curv.get_road().change_track(track,width);
    car_curv_sc.get_road().change_track(track,width);

    // Start from the steady-state values at 50km/h-0g    
    const scalar v = 50.0*KMH;
    auto ss = Steady_state(car_cart).solve(v,0.0,0.0); 

    ss.u[1] = 0.0;
    
    ss.dqdt = car_cart_sc(ss.q, ss.u, 0.0);

    Optimal_laptime opt_laptime(n_points, true, false, car_curv, ss.q, ss.u, {1.0e-2,200.0*200.0*1.0e-10});

    // Set outputs
    for (int i = 0; i < n_points; ++i)
    {
        const scalar& L = car_curv.get_road().track_length();
        car_curv_sc(opt_laptime.q[i], opt_laptime.u[i], ((double)i)*L/((double)n_points));
        x[i] = car_curv_sc.get_road().get_x();
        y[i] = car_curv_sc.get_road().get_y();
        delta[i] = opt_laptime.u[i][0]; 
        T[i] = opt_laptime.u[i][1]; 
    }
}

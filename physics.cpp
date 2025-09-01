#include <iostream>
#include <vector>
#include <thread>
#include <chrono>
#include <random>
#include <atomic>

// Standalone C++ program to simulate server load (e.g., physics + AI computations) with 100% CPU monopolization
// This runs multiple threads performing fake calculations without yielding, mimicking intensive game server tasks.
// Adjust num_threads and num_entities for load intensity. Runs for 60 seconds.
// WARNING: This will peg your CPU cores at 100% - use cautiously on a test machine to avoid overheating or freezes.
// Compile: g++ -o load_simulator load_simulator.cpp -std=c++11 -pthread
// Run: ./load_simulator

std::atomic<bool> running(true);  // Flag to control simulation loop

// Function simulating AI/pathfinding (simple random computations) - no yield for full monopolization
void simulateAI(int entity_id) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<> dis(0.0, 1.0);

    while (running) {
        double result = 0.0;
        for (int i = 0; i < 10000; ++i) {  // Simulate complex AI decisions
            result += dis(gen) * dis(gen);  // Fake math (e.g., pathfinding costs)
        }
        // No yield here - thread will run continuously
    }
}

// Function simulating physics (e.g., collision/force calculations) - no yield for full monopolization
void simulatePhysics(int entity_id) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<> dis(-1.0, 1.0);

    while (running) {
        double velocity = 0.0;
        double position = 0.0;
        for (int i = 0; i < 10000; ++i) {  // Simulate physics steps
            velocity += dis(gen) * 0.1;   // Fake force application
            position += velocity * 0.01;  // Update position
        }
        // No yield here - thread will run continuously
    }
}

int main() {
    const int num_threads = 8;      // Number of threads (simulate CPU cores)
    const int num_entities = 100;   // Number of "entities" per thread (e.g., players/AI)

    std::vector<std::thread> threads;

    std::cout << "Starting load simulation with " << num_threads << " threads and " << num_entities << " entities per thread." << std::endl;
    std::cout << "This will monopolize CPU - press Ctrl+C to stop or wait 60 seconds." << std::endl;

    // Spawn threads for AI and physics simulations
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([num_entities]() {
            for (int e = 0; e < num_entities; ++e) {
                simulateAI(e);     // Run AI sim
                simulatePhysics(e); // Run physics sim
            }
        });
    }

    // Run for 60 seconds
    std::this_thread::sleep_for(std::chrono::seconds(60));
    running = false;

    // Join threads
    for (auto& th : threads) {
        if (th.joinable()) th.join();
    }

    std::cout << "Simulation ended." << std::endl;
    return 0;
}

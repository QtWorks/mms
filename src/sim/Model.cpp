#include "Model.h"

#include <QPair>

#include "Assert.h"
#include "GeometryUtilities.h"
#include "Logging.h"
#include "Param.h"
#include "SimTime.h"
#include "SimUtilities.h"
#include "units/Duration.h"

namespace mms {

Model::Model() :
    m_shutdownRequested(false),
    m_maze(nullptr),
    m_mouse(nullptr),
    m_stats(nullptr),
    m_paused(false),
    m_simSpeed(1.0) {
    ASSERT_RUNS_JUST_ONCE();
}

void Model::start() {

    double prev = SimUtilities::getHighResTimestamp();
    double acc = 0.0;
    while (!m_shutdownRequested) {
        double now = SimUtilities::getHighResTimestamp();
        acc += (now - prev) * m_simSpeed;
        prev = now;
        while (acc >= DT) {
            update(DT);
            acc -= DT;
            // TODO: MACK - check for collisions ...
            // std::thread collisionDetector(&Model::checkCollision, this);
        }
        SimUtilities::sleep(Duration::Seconds(DT / 2.0));
    };
}

void Model::shutdown() {
    m_shutdownRequested = true;
}

void Model::update(double dt) {

    // Ensure the maze/mouse aren't updated in this loop
    m_mutex.lock();

    // If there's nothing to update, sleep for a little bit
    if (m_mouse == nullptr || m_paused) {
        m_mutex.unlock();
        return;
    }

    // Calculate the amount of sim time that should pass during this iteration
    Duration elapsedSimTimeForThisIteration = Duration::Seconds(dt);

    // Update the sim time
    SimTime::get()->incrementElapsedSimTime(elapsedSimTimeForThisIteration);

    // Update the position of the mouse
    m_mouse->update(elapsedSimTimeForThisIteration);

    // Retrieve the current discretized location of the mouse
    QPair<int, int> location = m_mouse->getCurrentDiscretizedTranslation();

    // If we're ever outside of the maze, crash. It would be cool to have
    // some "out of bounds" state but I haven't implemented that yet. We
    // continue here to make sure that we join with the other thread.
    if (!m_maze->withinMaze(location.first, location.second)) {
        m_mouse->setCrashed();
        m_mutex.unlock();
        return;
    }

    // Retrieve the tile at current location
    const Tile* tileAtLocation = m_maze->getTile(location.first, location.second);

    // If this is a new tile, update the set of traversed tiles
    if (!m_stats->traversedTileLocations.contains(location)) {
        m_stats->traversedTileLocations.insert(location);
        if (m_stats->closestDistanceToCenter == -1 ||
                tileAtLocation->getDistance() < m_stats->closestDistanceToCenter) {
            m_stats->closestDistanceToCenter = tileAtLocation->getDistance(); 
        }
        // Alert any listeners that a new tile was entered
        emit newTileLocationTraversed(location.first, location.second);
    }

    // If we've returned to the origin, reset the departure time
    if (location.first == 0 && location.second == 0) {
        m_stats->timeOfOriginDeparture = Duration::Seconds(-1);
    }

    // Otherwise, if we've just left the origin, update the departure time
    else if (m_stats->timeOfOriginDeparture < Duration::Seconds(0)) {
        m_stats->timeOfOriginDeparture = SimTime::get()->elapsedSimTime();
    }

    // Separately, if we're in the center, update the best time to center
    if (m_maze->isCenterTile(location.first, location.second)) {
        Duration timeToCenter = SimTime::get()->elapsedSimTime() - m_stats->timeOfOriginDeparture;
        if (
            m_stats->bestTimeToCenter < Duration::Seconds(0) ||
            timeToCenter < m_stats->bestTimeToCenter
        ) {
            m_stats->bestTimeToCenter = timeToCenter;
        }
    }

    // Release the mutex
    m_mutex.unlock();
}

void Model::setMaze(const Maze* maze) {
    m_mutex.lock();
    delete m_stats;
    m_stats = nullptr;
    m_mouse = nullptr;
    m_maze = maze;
    m_mutex.unlock();
}

void Model::setMouse(Mouse* mouse) {
    m_mutex.lock();
    ASSERT_FA(m_maze == nullptr);
    ASSERT_TR(m_mouse == nullptr);
    ASSERT_TR(m_stats == nullptr);
    m_mouse = mouse;
    m_stats = new MouseStats();
    SimTime::get()->reset();
    m_mutex.unlock();
}

void Model::removeMouse() {
    if (m_mouse == nullptr) {
        return;
    }
    m_mutex.lock();
    ASSERT_FA(m_maze == nullptr);
    ASSERT_FA(m_mouse == nullptr);
    ASSERT_FA(m_stats == nullptr);
    delete m_stats;
    m_stats = nullptr;
    m_mouse = nullptr;
    m_mutex.unlock();
}

MouseStats Model::getMouseStats() const {
    m_mutex.lock();
    MouseStats stats;
    if (m_stats != nullptr) {
        stats = *m_stats; 
    }
    m_mutex.unlock();
    return stats;
}

void Model::setPaused(bool paused) {
    m_paused = paused;
}

void Model::setSimSpeed(double factor) {
    m_simSpeed = factor;
}

void Model::checkCollision() {

    // If collision detectino isn't enabled, let this thread exit
    if (!P()->collisionDetectionEnabled()) {
        return;
    }

    // TODO: MACK
    // If the interface type is DISCRETE, let this thread exit
    if (true) {
        return;
    }

    /*
    while (true) {

        // In order to ensure we're sleeping the correct amount of time, we time
        // the collision detection operation and take it into account when we sleep.
        double start(mms::SimUtilities::getHighResTimestamp());

        // We declare these statically since we only need one copy of them
        static const Meters halfWallWidth = Meters(P()->wallWidth() / 2.0);
        static const Meters tileLength = Meters(P()->wallLength() + P()->wallWidth());

        // Retrieve the current collision polygon
        QVector<Cartesian> currentCollisionPolygonVertices =
            m_mouse->getCurrentCollisionPolygon(
                m_mouse->getCurrentTranslation(), m_mouse->getCurrentRotation()).getVertices();

        // Check for collisions
        for (int i = 0; i < currentCollisionPolygonVertices.size(); i += 1) {
            int j = (i + 1) % currentCollisionPolygonVertices.size();
            Cartesian v1 = currentCollisionPolygonVertices.at(i);
            Cartesian v2 = currentCollisionPolygonVertices.at(j);
            // If a wall has come between the two vertices, then we have a collision
            if (GeometryUtilities::castRay(v1, v2, *m_maze, halfWallWidth, tileLength) != v2) {
                S()->setCrashed();
                return; // If we've crashed, let this thread exit
            }
        }

        // Get the duration of the collision detection, in seconds
        double end(mms::SimUtilities::getHighResTimestamp());
        double duration = end - start;

        // Notify the use of a late collision detection
        // TODO: MACK - make some variables for these long expressions
        if (P()->printLateCollisionDetections() && duration > 1.0 / P()->collisionDetectionRate()) {
            qWarning().noquote().nospace()
                << "A collision detection was late by "
                << (duration - 1.0/P()->collisionDetectionRate())
                << " seconds, which is "
                << (duration - 1.0/P()->collisionDetectionRate())/(1.0/P()->collisionDetectionRate()) * 100
                << " percent late.";
        }

        // Sleep the appropriate amout of time, based on the collision detection duration
        mms::SimUtilities::sleep(mms::Seconds(std::max(0.0, 1.0 / P()->collisionDetectionRate() - duration)));
    }
    */
}

} // namespace mms

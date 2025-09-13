-- Minimal Sakila data for integration tests
-- This file provides just enough data to make tests pass
-- The benchmark and integration tests will create their own comprehensive data

-- Basic countries
INSERT INTO country (country, last_update) VALUES 
('United States', NOW()), 
('Canada', NOW()), 
('United Kingdom', NOW());

-- Basic cities  
INSERT INTO city (city, country_id, last_update) VALUES 
('New York', 1, NOW()), 
('Toronto', 2, NOW()), 
('London', 3, NOW());

-- Basic languages
INSERT INTO language (name, last_update) VALUES 
('English', NOW()), 
('Spanish', NOW());

-- Basic categories
INSERT INTO category (name, last_update) VALUES 
('Action', NOW()), 
('Comedy', NOW()), 
('Drama', NOW());

-- Basic actors
INSERT INTO actor (first_name, last_name, last_update) VALUES 
('John', 'Doe', NOW()), 
('Jane', 'Smith', NOW());

-- Basic films
INSERT INTO film (title, description, release_year, language_id, 
                 rental_duration, rental_rate, length, replacement_cost, rating, last_update) VALUES 
('Sample Movie 1', 'A test movie', 2023, 1, 3, 4.99, 120, 19.99, 'PG', NOW()),
('Sample Movie 2', 'Another test movie', 2022, 1, 5, 3.99, 95, 15.99, 'G', NOW());
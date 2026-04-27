CREATE TABLE mqtt_users (
    username TEXT PRIMARY KEY,
    password TEXT NOT NULL
);

INSERT INTO mqtt_users (username, password) VALUES
    ('alice', 'secret'),
    ('bob',   'hunter2');

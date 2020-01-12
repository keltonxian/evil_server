-- note: this is for testing, need to integrate into db-init.sql
-- DROP PROCEDURE register_user;
DROP PROCEDURE IF EXISTS register_user ;

delimiter //
CREATE PROCEDURE register_user (IN nn CHAR(100), IN pp CHAR(30), 
IN aa VARCHAR(30)) 
BEGIN
START TRANSACTION WITH CONSISTENT SNAPSHOT;
	IF aa <> '' THEN
		SET @cc = ( SELECT COUNT(1) FROM evil_user WHERE alias=aa );
	ELSE
		SET @cc = 0;
	END IF;
	IF @cc <= 0 OR aa = '' THEN -- empty alias ok
		INSERT INTO evil_user VALUES (NULL, nn, pp, aa, NOW());
		SELECT LAST_INSERT_ID();
	ELSE
		SELECT -1;
	END IF;
COMMIT;
END
//
delimiter ;


CALL register_user('t12', 'pass', 't13');


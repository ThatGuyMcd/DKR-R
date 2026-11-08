"""Immutable Dev Menu UI resource, generated into the SDK staging copy."""
def schema():
    def text(id,label):return dict(id=id,type='text',label=label)
    def button(id,label,action,op=None,confirm=False):
        value=dict(id=id,type='button',label=label,action=action)
        if op is not None:value['visible']='op_'+str(op)
        if confirm:value['confirm']=True
        return value
    def integer(id,label,argument,default=0,minimum=0,maximum=100):
        return dict(id=id,type='integer',label=label,argument=argument,default=default,min=minimum,max=maximum)
    def vector(id,label,argument):return dict(id=id,type='vector',label=label,argument=argument)
    def number(id,label,argument,default=0.0,minimum=-1000.0,maximum=1000.0):return dict(id=id,type='number',label=label,argument=argument,default=default,min=minimum,max=maximum)
    def table(id,label,select=False):return dict(id=id,type='table',label=label,select=select)
    def page(id,label,*fields):return dict(id=id,label=label,fields=list(fields))
    target=[dict(id='live-inspector',type='inspector',label='Live entity inspector',edit_action=101)]
    return dict(schema=2,title='DKR-R Dev Menu',pages=[
        page('session','Session',text('message','Last operation'),text('authority','Session authority'),
            text('scene','Scene generation'),text('tick','Simulation tick'),text('save_status','Save protection'),
            button('pause','Pause shared gameplay',20),button('resume','Resume / reset normal speed',21),button('step','Advance one update while paused',22),
            dict(id='speed',type='enum',label='Gameplay speed (retail logic units, not audio pitch)',argument=2,default=1,choices=['0.5x','1x','1.5x','2x']),button('set-speed','Apply gameplay speed',23),
            button('restart','Restart this scene',150,30,True),
            integer('restart-entrance','Existing entrance for this scene',2,0,0,15),button('restart-at','Restart at this entrance (all racer anchors required)',153,30,True),
            integer('race-laps','Ordinary race laps: 1..3 (0 resets)',2,3,0,3),button('apply-laps','Set laps before anyone completes the first lap',154,40,True),
            integer('course','Course catalogue index',2,0,0,1023),integer('vehicle','Vehicle: 0 Car / 1 Hovercraft / 2 Plane',3,0,0,2),
            integer('entrance','Entrance (other courses use their default spawn)',4,0,0,15),button('warp','Load this admitted course',151,31,True),
            dict(id='return-target',type='enum',label='Return using the normal game transition',argument=2,choices=['Track Select','Character Select','Start menu']),button('return-menu','Return to selected menu',152,32,True),
            button('reset','Reset Dev Menu racer overrides',2,None,True)),
        page('racers','Racers / AI',*target,integer('bananas','Bananas',2,10,0,99),button('set-bananas','Set selected racer bananas',105),
            integer('skill','AI skill',2,50,0,100),button('set-skill','Set selected racer AI skill',106),
            integer('ammo','Item ammunition',2,3,0,10),button('set-ammo','Set selected racer ammunition',107),
            button('stop','Stop selected racer',104),button('freeze','Toggle CPU freeze',160),
            button('boost','Toggle selected racer continuous boost',161),button('shield','Toggle selected racer shield timer',162),
            button('keep-bananas','Toggle selected racer 10 bananas',163),text('overrides','Active overrides'),
            number('weight','Selected racer weight / gravity factor',2,1,0.1,4),number('handling','Handling factor',3,1,0.1,4),number('acceleration','Acceleration curve factor (standard vehicles)',4,1,0.1,4),number('speed-cap','Forward speed cap (0 = off)',5,0,0,100),
            button('apply-tune','Apply selected racer tuning',182),button('reset-tune','Reset this tool\'s racer tuning',181,None,True)),
        page('objects','Objects',*target,
            integer('property','Property ID (see SDK reference)',2,102,1,301),button('inspect','Inspect selected property',100),
            integer('value','Integer property value',3,10,-32768,65535),button('edit','Apply selected integer property',101),
            dict(id='float-value',type='number',label='Float property value',argument=3,default=1.0,min=-1000.0,max=1000.0),button('edit-float','Apply selected float property',101),
            button('undo','Undo last property edit',102),button('redo','Redo property edit',103),
            vector('position','Destination / clone position: X Y Z',2),button('teleport','Teleport selected entity',110,1),
            button('clone','Clone selected scenery template',111,2),button('delete','Delete selected tool-owned spawn',112,3,True)),
        page('track','Track / Routes',integer('catalogue-page','First course catalogue index (8 entries per page)',2,0,0,1016),
            button('read-catalogue','Read this course page',12),table('courses','Admitted stock, legacy and Track Lab courses'),
            dict(id='debug-display',type='debug',label='Show local route, selected entity and probe overlays'),
            dict(id='scene-catalogue',type='enum',label='Loaded-scene inspector',argument=2,choices=['Scene / rules','Segment bounds','AI node links','Spawn points / entrances','Trigger volumes','Triangle geometry']),
            integer('scene-entry','First scene entry (4 per page)',3,0,0,8191),integer('scene-segment','Segment for triangle inspection',4,0,0,1023),button('inspect-scene','Read current-scene metadata',17),table('geometry','Loaded scene, geometry and navigation'),
            integer('draw-segment','Segment to highlight',2,0,0,1023),integer('draw-triangle','Triangle to highlight',3,0,0,8191),
            dict(id='geometry-mode',type='enum',label='Overlay highlight',argument=4,default=0,choices=['Racing route','Segment bounds','Triangle wireframe / normal']),button('draw-geometry','Update geometry highlight',18),
            integer('route-page','Checkpoint index / first route on this page',2,0,0,126),button('read-route','Inspect native checkpoint / AI racing line',14),table('routes','Native main and alternate racing routes',True),
            integer('checkpoint-index','Checkpoint to move',2,0,0,126),vector('checkpoint-position','Checkpoint position: X Y Z',3),
            integer('checkpoint-yaw','Yaw (native angle16)',6,0,-32768,32767),integer('checkpoint-pitch','Pitch (native angle16)',7,0,-32768,32767),integer('checkpoint-roll','Roll (native angle16)',8,0,-32768,32767),
            number('checkpoint-scale','Checkpoint scale',9,1.0,0.01,16.0),button('move-checkpoint','Move checkpoint and rebuild native route planes',171,4,True),
            button('prepare-checkpoint','Prepare the inspected checkpoint as a patch',183),dict(id='checkpoint-document',type='document',label='Export or review a checkpoint adjustment',document='checkpoint-patch',import_action=184),
            vector('probe-from','Probe from: X Y Z',2),vector('probe-to','Probe to: X Y Z',5),number('probe-radius','Probe radius (0 = ray)',8,0,0,512),
            integer('probe-flags','0 Static / 1 Static + interaction spheres / 3 Spheres only',9,0,0,3),button('cast','Run bounded collision probe',170),text('probe','Probe result'),
            button('prepare-document','Prepare property-edit / bookmark document',179),dict(id='scene-document',type='document',label='Export or review a scene adjustment document',document='scene-patch',import_action=180),
            text('entity_count','Live entity count'),text('racer_count','Current racer count'),text('message','Last operation')),
        page('camera','Camera / Bookmarks',*target,button('bookmark','Mark selected entity position',120),
            button('return','Return selected entity to marked position',121,1),
            dict(id='local-camera',type='camera',label='Local free, follow and spectator camera')),
        page('environment','Environment / Audio',integer('fog-player','Fog viewport (0..3)',2,0,0,3),
            integer('near','Fog near',3,900,0,1018),integer('far','Fog far',4,1000,5,1023),
            integer('red','Fog red',5,128,0,255),integer('green','Fog green',6,128,0,255),integer('blue','Fog blue',7,128,0,255),
            button('fog','Apply original game fog',130,10),*target,
            number('ambient','Selected object ambient light',2,1.0,0,2),button('ambient-apply','Apply selected object ambient light',190),
            number('diffuse','Selected object diffuse light',2,1.0,0,2),button('diffuse-apply','Apply selected object diffuse light',191),
            integer('weather-x','Existing weather X velocity',2,0,-1048576,1048576),integer('weather-y','Existing weather Y velocity',3,-256,-1048576,1048576),integer('weather-z','Existing weather Z velocity',4,0,-1048576,1048576),
            integer('weather-intensity','Weather intensity',5,16384,0,65536),integer('weather-opacity','Weather opacity',6,32768,0,65536),integer('weather-transition','Weather transition ticks',7,120,1,3600),button('weather','Adjust initialized scene weather',131,11),
            integer('sound','Sound / sequence ID',2,0,0,65535),
            button('sound-play','Play sound',140,20),button('music-play','Play music sequence',141,21),
            button('music-stop','Stop music',142,22),integer('fade-value','Music fade (-4096..4096)',2,-60,-4096,4096),button('fade','Fade music',143,23)),
        page('mods','Mods / Values',integer('package-page','First code package index (8 per page)',2,0,0,120),button('read-packages','Read this package page',13),
            table('packages','Active portable code packages and capabilities'),dict(id='reload-code',type='reload',label='Offline compatible code reload'),text('instructions','Remaining callback instruction budget'),
            dict(id='resource-kind',type='enum',label='Resource catalogue',argument=2,choices=['Characters','Character models / headers','Object models','3D textures','2D textures','Sprites','Object templates','Level models','Custom cups','Sounds','Music sequences','All mod settings','Hooks','Callbacks','This mod resources']),
            integer('resource-page','First resource index (8 per page)',3,0,0,16376),button('read-resources','Browse this resource page',16),table('resources','Verified resource identities and live-setting policy'),
            integer('setting-package','Setting owner package index',2,0,0,127),integer('setting-option','Option index in that package',3,0,0,127),
            number('setting-number','Numeric setting value',4,1,-1e6,1e6),button('apply-setting-number','Apply opted-in numeric setting',200),
            integer('setting-choice','Enum choice index',4,0,0,255),button('apply-setting-enum','Apply opted-in enum setting',201),button('reset-setting','Reset selected live setting',202),
            text('settings-policy','Live setting policy'),text('watch','Selected property watch'),
            text('native_calls','Remaining native-call budget'),text('last_action','Last deterministic command ID'),dict(id='costs',type='diagnostics',label='Per-package work and memory'),
            integer('symbol-page','First decomp symbol index (12 entries per page)',2,0,0,8192),button('read-symbols','Read reviewed symbol / hook capabilities',15),table('symbols','Decomp symbols: callable, hookable or protected'),table('trace','Recent deterministic lifecycle/racer events')),
        page('commands','Commands / History',text('message','Last command result'),text('last_action','Last deterministic command ID'),
            dict(id='command-console',type='console',label='Registered SDK commands and recorded macros'),
            button('refresh','Refresh tools snapshot',1),button('undo','Undo last typed property edit',102),button('redo','Redo typed property edit',103)),
        page('online','Online Development',text('authority','Command authority'),text('save_status','Development save scope'),
            dict(id='online-authority',type='authority',label='Host permissions, pending requests and shared action audit'),
            text('scene','Scene generation'),text('tick','Current simulated tick'),text('last_action','Last ordered action'),text('message','Last operation'))
    ])

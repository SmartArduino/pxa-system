// SPDX-License-Identifier: MIT
// Native samples enter through the page; Guests retain the ordinary Sensor ABI.
export const SENSOR_SCOPES=['motion.acceleration','motion.angular-velocity'];
export function installBrowserSensors(host,notify,permissions=[],available=0,clock=()=>performance.now()){
  host.browserSensorMask=SENSOR_SCOPES.reduce((mask,scope,i)=>mask|((available&(1<<i))&&permissions.some(p=>p.name==='sensor.read'&&p.scope===scope)?1<<i:0),0);
  const refs=[0,0],latest=[null,null];let epoch=0,mask=0,paused=false;
  const update=()=>{
    const next=paused?0:refs.reduce((m,n,i)=>m|(n?1<<i:0),0);
    if(next===mask)return;
    if(!next){
      if(mask)notify({type:'sensors-stop',id:epoch});
      mask=0;latest.fill(null);return;
    }
    // A new channel must not tear down an existing native stream or its grant.
    if(!mask){epoch++;latest.fill(null);}
    else for(let i=0;i<2;i++)if(!(next&(1<<i)))latest[i]=null;
    mask=next;notify({type:'sensors-request',id:epoch,mask});
  };
  host.browserSensorSubscribe=id=>{
    if(id<1||id>2||!(host.browserSensorMask&(1<<(id-1))))return -3;
    refs[id-1]++;update();return 0;
  };
  host.browserSensorUnsubscribe=id=>{if(id>=1&&id<=2&&refs[id-1]){refs[id-1]--;update();}};
  host.browserSensorRead=(id,pointer)=>{
    const sample=latest[id-1];
    if(!sample||clock()-sample.at>250)return -7;
    for(let i=0;i<3;i++)host.HEAP32[(pointer>>>2)+i]=sample.values[i];
    return 0;
  };
  return {
    sample(id,readings){
      if(id!==epoch||!mask||!Array.isArray(readings)||readings.length>2)return;
      for(const reading of readings){const i=reading?.id-1,v=reading?.values;
        if((i!==0&&i!==1)||!(mask&(1<<i))||!Array.isArray(v)||v.length!==3||!v.every(n=>Number.isInteger(n)&&n>=-2147483648&&n<=2147483647))continue;
        latest[i]={values:v.slice(),at:clock()};
      }
    },
    clear(id){if(id===epoch)latest.fill(null);},
    pause(value){paused=!!value;update();},
    close(){refs.fill(0);update();},
  };
}

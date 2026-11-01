// SPDX-License-Identifier: MIT
// Native samples enter through the page; Guests retain the ordinary Sensor ABI.
export const SENSOR_SCOPES=['motion.acceleration','motion.angular-velocity','magnetic-field.0'];
export function installBrowserSensors(host,notify,permissions=[],available=0,clock=()=>performance.now(),timeOrigin=performance.timeOrigin){
  host.browserSensorMask=SENSOR_SCOPES.reduce((mask,scope,i)=>mask|((available&(1<<i))&&permissions.some(p=>p.name==='sensor.read'&&p.scope===scope)?1<<i:0),0);
  const count=SENSOR_SCOPES.length,refs=Array(count).fill(0),latest=Array(count).fill(null);let epoch=0,mask=0,paused=false;
  const update=()=>{
    const next=paused?0:refs.reduce((m,n,i)=>m|(n?1<<i:0),0);
    if(next===mask)return;
    if(!next){
      if(mask)notify({type:'sensors-stop',id:epoch});
      mask=0;latest.fill(null);return;
    }
    // A new channel must not tear down an existing native stream or its grant.
    if(!mask){epoch++;latest.fill(null);}
    else for(let i=0;i<count;i++)if(!(next&(1<<i)))latest[i]=null;
    mask=next;notify({type:'sensors-request',id:epoch,mask});
  };
  host.browserSensorSubscribe=id=>{
    if(!Number.isInteger(id)||id<1||id>count||!(host.browserSensorMask&(1<<(id-1))))return -3;
    refs[id-1]++;update();return 0;
  };
  host.browserSensorUnsubscribe=id=>{if(id>=1&&id<=count&&refs[id-1]){refs[id-1]--;update();}};
  host.browserSensorRead=(id,pointer)=>{
    const sample=latest[id-1];
    if(!sample||clock()-sample.at>250)return -7;
    for(let i=0;i<3;i++)host.HEAP32[(pointer>>>2)+i]=sample.values[i];
    return 0;
  };
  host.browserSensorTimestamp=id=>latest[id-1]?.at??0;
  return {
    sample(id,readings){
      if(id!==epoch||!mask||!Array.isArray(readings)||readings.length>count)return;
      for(const reading of readings){const i=reading?.id-1,v=reading?.values;
        if((!Number.isInteger(i)||i<0||i>=count)||!(mask&(1<<i))||!Array.isArray(v)||v.length!==3||!v.every(n=>Number.isInteger(n)&&n>=-2147483648&&n<=2147483647))continue;
        const age=reading.ageMs??0;
        if(typeof age!=='number'||!Number.isFinite(age)||age<0||age>250)continue;
        const now=clock(),at=reading.timestampMs===undefined?now-age:reading.timestampMs-timeOrigin;
        if((reading.timestampMs!==undefined&&typeof reading.timestampMs!=='number')||!Number.isFinite(at)||at<0||at>now||now-at>250||(latest[i]&&at<latest[i].at))continue;
        latest[i]={values:v.slice(),at};
      }
    },
    clear(id){if(id===epoch)latest.fill(null);},
    pause(value){paused=!!value;update();},
    close(){refs.fill(0);update();},
  };
}

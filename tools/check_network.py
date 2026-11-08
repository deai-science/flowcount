"""Check the HTTP API actually served by the ESP32, without changing its network.

python tools/check_network.py --url http://192.168.4.1 --access-file .private/network-access.json
"""
import argparse
import base64
import json
from pathlib import Path
import time
from urllib.request import Request, urlopen
from urllib.error import HTTPError


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--url',required=True)
    parser.add_argument('--access-file',type=Path,help='Compatibility option; the web UI uses admin/admin, not the WPA2 key.')
    parser.add_argument('--timeout',type=float,default=8)
    parser.add_argument('--scan',action='store_true',help='Check network scanning (may briefly slow the preview)')
    args=parser.parse_args()
    base=args.url.rstrip('/')
    authorization='Basic '+base64.b64encode(b'admin:admin').decode()

    def request(path,body=None,authenticated=True,origin=None):
        headers={'Authorization':authorization} if authenticated else {}
        if origin:
            headers['Origin']=origin
        if body is not None:
            headers['Content-Type']='application/json'
        req=Request(base+path,headers=headers,
                    data=json.dumps(body).encode() if body is not None else None)
        try:
            with urlopen(req,timeout=args.timeout) as response:
                return response.status,response.headers,response.read()
        except HTTPError as exc:
            return exc.code,exc.headers,exc.read()

    assert request('/state',authenticated=False)[0]==401,'State exposed without authentication'
    assert request('/command',{'command':'save'},authenticated=False)[0]==401,'Commands exposed without authentication'
    assert request('/networks',authenticated=False)[0]==401,'Network scan data exposed without authentication'
    code,_,html=request('/')
    assert code==200 and b'networkForm' in html and b'FlowCount' in html and b'mainNav' in html,'Embedded UI missing'
    code,_,raw=request('/state')
    first=json.loads(raw)
    assert code==200 and first['connected'] and first['state']['frame']>0,'Inference is not active'
    time.sleep(2)
    code,_,raw=request('/state')
    second=json.loads(raw)
    assert second['state']['frame']>first['state']['frame'],'Frames are not advancing'
    code,headers,jpeg=request('/image.jpg')
    assert code==200 and jpeg.startswith(b'\xff\xd8') and b'\xff\xd9' in jpeg,'JPEG missing'
    metadata=json.loads(headers['X-Frame-State'])
    assert metadata['frame']>0 and isinstance(metadata['tracks'],list),'Image metadata missing'
    code,new_headers,new_jpeg=request('/image.jpg?after='+str(metadata['frame']))
    assert code in (200,204),'Conditional image request failed'
    if code==204:
        assert not new_jpeg,'Repeated frame transferred a body'
    else:
        assert json.loads(new_headers['X-Frame-State'])['frame']!=metadata['frame'],'Repeated image sent again'
        assert new_jpeg.startswith(b'\xff\xd8'),'New image missing'
    assert json.loads(request('/command',{'command':'save'})[2])['ok'],'Saving counters failed'
    assert not json.loads(request('/command',{'command':'unexpected-command-for-test'})[2])['ok'],'Unknown command accepted'
    assert not json.loads(request('/command',{'command':'replay end'})[2])['ok'],'USB replay exposed over HTTP'
    assert not json.loads(request('/command',{'command':'save'},origin='http://invalid.example')[2])['ok'],'Cross-origin command accepted'
    assert not json.loads(request('/network',{'ssid':'','password':''})[2])['ok'],'Invalid Wi-Fi configuration accepted'
    scanned=None
    if args.scan:
        assert json.loads(request('/scan',{})[2])['ok'],'Network scan rejected'
        deadline=time.monotonic()+22
        while time.monotonic()<deadline:
            time.sleep(.5)
            code,_,raw=request('/networks');scan=json.loads(raw)
            if not scan['scanning']:
                assert not scan.get('error'),scan.get('error')
                assert isinstance(scan['networks'],list)
                scanned=len(scan['networks'])
                break
        assert scanned is not None,'Scan did not finish'
    print(json.dumps({'result':'PASS','url':base,'first_frame':first['state']['frame'],
        'last_frame':second['state']['frame'],'jpeg_bytes':len(jpeg),'image_frame':metadata['frame'],
        'inference_ms':second['state']['inference_ms'],'ap_ssid':second['network']['ap_ssid'],
        'sta_connected':second['network']['sta_connected'],'scanned_networks':scanned},indent=2))


if __name__=='__main__':
    main()

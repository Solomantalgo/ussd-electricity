const express = require('express');
const sqlite3 = require('sqlite3').verbose();
const app = express();
const port = 3000;
const path = require('path');

//connect the database
const db = new sqlite3.Database('./database.sqlite', (err) => {
    if (err) {
        console.log(`failed to connect to the database ${err.message}`)
    }
    else {
        console.log(`connected to the database`)
    }
});

//create the transaction table if not exist
db.run(`
    CREATE TABLE IF NOT EXISTS transactions (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        Phone_number TEXT NOT NULL,
        meter_number TEXT NOT NULL,
        token TEXT NOT NULL,
        amount INTEGER NOT NULL,
        status TEXT NOT NULL
    )
`)

app.use(express.urlencoded({ extended: false }))

//serve the static file
app.use(express.static(path.join('../USSD-SIMULATED-UI')))

//Route for the simulated server
app.get('/simulate', (req, res) => {
    res.sendFile(path.join(__dirname, '../USSD-SIMULATED-UI/simulate.html'))
})


app.post('/ussd', (req, res) => {
    // Read the data sent by the telecom
    const { sessionId, ServiceCode, phoneNumber, text } = req.body;

    let response = '';

    //if the text is empty that means user just dialed the code
    if (text === '') {
        response = `CON welcome to the UEDCL electricity service
        1.Buy Electricity Token
        2.Check Balance
        `
    }

    

    //if the user selected 1
    else if (text === '1') {
        response = `CON Please enter your Meter number`;
    }


    //if the user entered 1 and then meter number
    else if (text.startsWith('1*')) {

        if (text.split('*').length === 2){
        response = `CON Please enter the amount you want to buy`;
        } else {
             //get the mock up token
        let generatedToken =Math.floor(Math.random()* 1e20).toString().padStart(20, '0');

        //user amount 
        let amount = text.split('*')[2];
        if(isNaN(amount) || Number(amount) <= 0){
            response = 'END Invalid amount. Please enter a valid number.'
        } else {

        let status = 'Unused';
        //get the meter number
        let meterNumber = text.split('*')[1];

        //insert into the Database
        db.run(
            'INSERT INTO transactions (Phone_number, meter_number, token, amount, status) VALUES (?,?,?,?,?)',
            [phoneNumber, meterNumber, generatedToken, amount, status],
            function (err) {
                if (err) {
                    console.error("Error saving transaction", err.message);
                } else {
                    console.log(`Transaction saved successfully with id ${this.lastID}`)
                }
            }
        );

        response = `END Thank you generating token for meter number ${meterNumber}. Token is ${generatedToken}
        You will receive an sms shortly`;
        } 
    }
    }

    //if the user selected 2
    else if (text === '2') {
        response = 'END your balance is UGX 50,000'
    }

    //if invalid option
    else {
        response = 'END Invalid option'
    }

    //send the response back to the telecom
    res.set('content-type', 'text/plain');
    res.send(response);
});


app.get('/', (req, res) => {
    res.send('Welcome to the USSD Electricty Backend');
});

//Route to view all transactions
//TEST
app.get('/transactions', (req, res) => {
    db.all('SELECT * FROM transactions', [], (err, rows) => {
        if(err){
            res.status(500).json({error: err.message})
            return;
        }
        res.json(rows);
    })
});

//Esp32 route 
app.get('/api/tokens/:meterNumber', (req, res) => {
    const meterNumber = req.params.meterNumber;

    //Search the database for the token 
    db.get(
        `SELECT token, amount FROM transactions WHERE meter_number = ? AND status = 'Unused' ORDER BY ID ASC LIMIT 1`,
        [meterNumber],
        (err, row) => {
            if(err){
                res.status(500).json({error:err.message})
                return;
            }
            if(row){
                //Generate units
             const units = Math.floor(row.amount/1000)

                //if token is found send it to the Esp32
                res.json({token:row.token, hasNewToken:true, units:units});
            } else {
                //if no token found, tell the esp32 to keep waiting
                res.json({hasNewToken:false})
            }
        }
    )
});

//Route to activate tokens
app.post('/api/tokens/activate', (req, res) =>{
    const {meterNumber, token} = req.body;

    db.run(
        `UPDATE transactions set status = 'Active'  where meter_number = ? AND token = ? `,
        [meterNumber, token],
        function(err){
            if(err){
                res.status(500).json({error:err.message})
                return;
            }
            res.json({success:true, message:'token activated successfully'})
        }
    )

    
})

//Route to update token status after use
app.post('/api/tokens/use', (req, res) => {
    const {meterNumber, token} = req.body

    db.run(
        `UPDATE transactions SET status = 'Used' WHERE meter_number = ? AND token = ?`,
    [meterNumber, token],
    function(err){
        if(err){
            res.status(500).json({error:err.message});
            return;
        }
     res.json({success:true, message:'Token marked as used'});
    }
 );
});

app.listen(port, () => {
    console.log(`Server is running on port ${port}`);
});